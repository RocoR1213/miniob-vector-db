// Panda IVFFlat索引类
#pragma once

#include "storage/index/index.h"
#include <vector>
#include <cmath>

using namespace std;

class IvfflatIndex : public Index
{
public:
  // 这些都是继承父类的方法，必须全部实现

  // 构造函数
  IvfflatIndex()  = default;

  // 析构函数
  virtual ~IvfflatIndex() noexcept = default;

  // 创建索引
  RC create(Table *table, const char *file_name,
            const IndexMeta &index_meta, const FieldMeta &field_meta) override;

  // 打开索引
  RC open(Table *table, const char *file_name,
          const IndexMeta &index_meta, const FieldMeta &field_meta) override;
  
  // 关闭索引
  RC close();

  // 插入索引项
  RC insert_entry(const char *record, const RID *rid) override;

  // 删除索引项
  RC delete_entry(const char *record, const RID *rid) override;

  // 同步索引
  RC sync() override;

  // B+树扫描器 向量索引不需要 一直返回nullptr即可
  IndexScanner *create_scanner(const char *left_key, int left_len, bool left_inclusive,
                               const char *right_key, int right_len,
                               bool right_inclusive) override;

  // 向量索引校验
  bool is_vector_index() override { return true; }

  // K_Means检索向量索引查询器入口
  vector<RID> ann_search(const vector<float> &query_vector,
                         size_t limit, int probes);

private:
  // K-Means训练
  RC    do_kmeans(const vector<vector<float>> &vectors);

  // 计算2范数距离
  float l2_distance(const vector<float> &a, const vector<float> &b);

  // 查找聚类中心 int量化
  int   nearest_center(const vector<float> &vec);

  // 索引状态
  bool   inited_ = false;
  Table *table_  = nullptr;
  int    lists_  = 1;
  int    probes_ = 1;
  int    dim_    = 0;

  // 索引数据
  vector<vector<float>> centers_;         // 聚类中心 [lists_][dim_]
  vector<vector<RID>>   inverted_lists_;  // 倒排列表 [lists_]
};