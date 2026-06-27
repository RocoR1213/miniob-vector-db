// Panda 实现ivfflat索引的实现文件
#include "storage/index/ivfflat_index.h"
#include "common/log/log.h"
#include "storage/table/table.h"
#include "storage/record/record_scanner.h"
#include "storage/field/field_meta.h"

#include <algorithm>
#include <random>
#include <fstream>
#include <cstring>

// Private

// L2范数计算 d = ∑(a[i] - b[i])^2
float IvfflatIndex::l2_distance(const vector<float> &a, const vector<float> &b) {
    float sum = 0;
    for (int i = 0; i < a.size(); i++) {
        float d = a[i] - b[i];
        sum += d * d;
    }
    return sqrt(sum);
}

// 找最近聚类中心
int IvfflatIndex::nearest_center(const vector<float> &vec) {
    int best = 0;
    float best_dist = l2_distance(vec, centers_[0]);
    for (int i = 1; i < centers_.size(); i++) {
        float d = l2_distance(vec, centers_[i]);
        if (d < best_dist) {
            best = i;
            best_dist = d;
        }
    }
    /* TODO 这里直接舍去尾数 考虑更好的精度量化*/
    return best;
}

// K-Means训练
RC IvfflatIndex::do_kmeans(const vector<vector<float>> &vectors) {
    int n = vectors.size();
    // 异常值处理
    if (n == 0) { return RC::SUCCESS; }

    dim_ = vectors[0].size();

    // STAGE 1 初始化聚类中心

    // 随机选lists_个点作为初始聚类中心
    centers_.assign(lists_, vector<float>(dim_));
    vector<int> indices(n);
    for (int i = 0; i < n; i++) {
        indices[i] = i;
    }
    random_device rd;
    mt19937 g(rd());    // 用 std::mt19937 (梅森旋转算法) 随机打乱下标
    shuffle(indices.begin(), indices.end(), g);
    for (int i = 0; i < min(lists_, n); i++) {
        centers_[i] = vectors[indices[i]];
    }

    // STAGE 2 迭代 E-M 算法
    vector<int> assignments(n);
    for (int iter = 0; iter < sqrt(n) + 1; iter++) {    // 凭直觉，感觉sqrt(n) + 1 会是一个很好的迭代次数
        // 分配每个向量到最近的中心
        for (int i = 0; i < n; i++) {
            assignments[i] = nearest_center(vectors[i]);
        }

        // 基于每个中心的向量，重新计算每个簇的均值
        // 累加
        vector<vector<float>> new_centers(lists_, vector<float>(dim_, 0));
        vector<int> counts(lists_, 0);
        for (int i = 0; i < n; i++) {   // 分配中心 c统计每个中心的向量数量
            int c = assignments[i];
            counts[c]++;
            for (int d = 0; d < dim_; d++) {
                new_centers[c][d] += vectors[i][d];
            }
        }
        bool changed = false;

        // 除得均值，即簇中心
        for (int c = 0; c < lists_; c++) {
            if (counts[c] > 0) {
                for (int d = 0; d < dim_; d++) {
                    new_centers[c][d] /= counts[c];
                }
            }
            // 判断收敛
            if (l2_distance(centers_[c], new_centers[c]) > 1e-2f) { // 阈值为1e-2f时提前停
                changed = true;
            }
            centers_[c] = new_centers[c];
        }

        // 如果没有变化，提前停
        if (!changed) { break;}
    }

    return RC::SUCCESS;
}

// Public

// 索引创建
RC IvfflatIndex::create(Table *table, const char *file_name,
                        const IndexMeta &index_meta, const FieldMeta &field_meta) {
    table_ = table;
    lists_ = index_meta.lists();
    probes_ = index_meta.probes();
    if (lists_ <= 0) lists_ = 1;   // 如果lists <= 0, 兜底为1, 防御性编程

    // STAGE 1

    // 扫描全表收集所有向量
    RecordScanner *scanner = nullptr;
    RC rc = table->get_record_scanner(scanner, nullptr, ReadWriteMode::READ_ONLY);
    if (rc != RC::SUCCESS) { 
        LOG_ERROR("ivfflat: failed to get scanner. rc=%s", strrc(rc));
        return rc;
    }

    // 获取向量
    vector<vector<float>> all_vectors;
    vector<RID>           all_rids;
    Record record;
    while ((rc = scanner->next(record)) == RC::SUCCESS) {
        const char *data = record.data();
        const float *vec_data = reinterpret_cast<const float *>(data + field_meta.offset());
    int dim = field_meta.len() / sizeof(float);
    vector<float> vec(vec_data, vec_data + dim);
    all_vectors.push_back(vec);
    all_rids.push_back(record.rid());
    }
    scanner->close_scan();
    delete scanner;

    // 异常处理
    if (rc != RC::RECORD_EOF) {
        LOG_ERROR("ivfflat: scan failed. rc=%s", strrc(rc));
        return rc;
    }
    if (all_vectors.empty()) {
        return RC::SUCCESS;
    }
    dim_ = all_vectors[0].size();

    // STAGE 2

    // 进行K-Means训练
    rc = do_kmeans(all_vectors);
    if (rc != RC::SUCCESS) { return rc;}

    // STAGE 3

    // 构建倒排列表
    inverted_lists_.assign(lists_, vector<RID>());
    for (int i = 0; i < all_vectors.size(); i++) {
        int c = nearest_center(all_vectors[i]);
        inverted_lists_[c].push_back(all_rids[i]);
    }

    // STAGE 4

    // 保存文件 二进制
    index_meta_ = index_meta;
    field_meta_ = field_meta;

    ofstream ofs(file_name, ios::binary);
    if (!ofs) {
        LOG_ERROR("ivfflat: cannot open file %s", file_name);
        return RC::IOERR_OPEN;
    }
    ofs.write(reinterpret_cast<const char *>(&lists_), sizeof(lists_));
    ofs.write(reinterpret_cast<const char *>(&probes_), sizeof(probes_));
    ofs.write(reinterpret_cast<const char *>(&dim_), sizeof(dim_));
    for (int c = 0; c < lists_; c++) {
        int center_size = dim_;
        ofs.write(reinterpret_cast<const char *>(&center_size), sizeof(center_size));
        ofs.write(reinterpret_cast<const char *>(centers_[c].data()), centers_[c].size() * sizeof(float));
        int list_size = inverted_lists_[c].size();
        ofs.write(reinterpret_cast<const char *>(&list_size), sizeof(list_size));
        for (const RID &rid : inverted_lists_[c]) {
            ofs.write(reinterpret_cast<const char *>(&rid), sizeof(RID));
        }
    }
    ofs.close();
    inited_ = true;
    return RC::SUCCESS;
}

// 从文件加载索引
RC IvfflatIndex::open(Table *table, const char *file_name,
    const IndexMeta &index_meta, const FieldMeta &field_meta) {
    table_ = table;
    index_meta_ = index_meta;
    field_meta_ = field_meta;

    // 读取文件 二进制
    ifstream ifs(file_name, ios::binary);
    if (!ifs) {
    LOG_ERROR("ivfflat: cannot open file %s", file_name);
    return RC::IOERR_OPEN;
    }
    
    // 元信息
    ifs.read(reinterpret_cast<char *>(&lists_), sizeof(lists_));
    ifs.read(reinterpret_cast<char *>(&probes_), sizeof(probes_));
    ifs.read(reinterpret_cast<char *>(&dim_), sizeof(dim_));
    
    // 向量
    centers_.resize(lists_);
    inverted_lists_.resize(lists_);
    for (int c = 0; c < lists_; c++) {
        int center_size;
        ifs.read(reinterpret_cast<char *>(&center_size), sizeof(center_size));
        centers_[c].resize(center_size);
        ifs.read(reinterpret_cast<char *>(centers_[c].data()), center_size * sizeof(float));
        int list_size;
        ifs.read(reinterpret_cast<char *>(&list_size), sizeof(list_size));
        inverted_lists_[c].resize(list_size);
        for (int i = 0; i < list_size; i++) {
            ifs.read(reinterpret_cast<char *>(&inverted_lists_[c][i]), sizeof(RID));
        }
    }
    ifs.close();
    inited_ = true;
    return RC::SUCCESS;
}

// 关闭索引
RC IvfflatIndex::close() {
    inited_ = false;
    return RC::SUCCESS;
}

// 插入索引
RC IvfflatIndex::insert_entry(const char *record, const RID *rid) {
    if (!inited_) { return RC::SUCCESS;}    // 理论上不应该有这种情况发生，防御性编程

    const float *vec_data = reinterpret_cast<const float *>(record + field_meta_.offset());
    vector<float> vec(vec_data, vec_data + dim_);
    int c = nearest_center(vec);
    inverted_lists_[c].push_back(*rid);
    return RC::SUCCESS;
}

// 删除索引
RC IvfflatIndex::delete_entry(const char *, const RID *) {
    return RC::UNIMPLEMENTED;
}

// 同步索引
RC IvfflatIndex::sync() {
    // create() 中已写入完整索引文件，无需额外同步
    return RC::SUCCESS;
}

// B+树扫描器
IndexScanner *IvfflatIndex::create_scanner(const char *, int, bool, const char *, int, bool){
  // 向量索引不使用 B+Tree 扫描器 直接返回 nullptr
  return nullptr;
}

// ann 向量索引搜索器
vector<RID> IvfflatIndex::ann_search(const vector<float> &query_vector, size_t limit, int probes) {
    vector<RID> result;
    if (!inited_) { return result;}

    // 找出最近的probes个聚类中心
    vector<pair<float, int>> center_dists;
    for (int c = 0; c < lists_; c++) {
        center_dists.push_back({l2_distance(query_vector, centers_[c]), c});
    }
    sort(center_dists.begin(), center_dists.end());
    int actual_probes = min(probes, lists_);    // 防御性编程

    // 在选择的簇中遍历 暴力搜索
    vector<pair<float, RID>> candidates;
    for (int p = 0; p < actual_probes; p++) {
        int c_idx = center_dists[p].second;
        for (const RID &rid : inverted_lists_[c_idx]) {
            // RID近似精确距离 待检查点C回表计算
            candidates.push_back({center_dists[p].first, rid});
        }
    }
    // 按距离排序（只比较float，不比较RID）
    sort(candidates.begin(), candidates.end(),
        [](const pair<float, RID> &a, const pair<float, RID> &b) {
            return a.first < b.first;
            });
    
    for (int i = 0; i < min(limit, candidates.size()); i++) {
        result.push_back(candidates[i].second);
    }
    return result;
}