#include "sql/operator/vector_index_scan_physical_operator.h"
#include "storage/index/ivfflat_index.h"
#include "storage/table/table.h"
#include "storage/record/record.h"

// 构造函数
VectorIndexScanPhysicalOperator::VectorIndexScanPhysicalOperator(
    Table *table, IvfflatIndex *index, const vector<float> &query_vector, int limit, int probes)
    : table_(table), index_(index), query_vector_(query_vector), limit_(limit), probes_(probes) {}

// 打开算子
RC VectorIndexScanPhysicalOperator::open(Trx *trx) {
    // 参数校验
    if (table_ == nullptr || index_ == nullptr) {
        return RC::INVALID_ARGUMENT;
    }

    // 调ann_search获得候选rid
    rids_ = index_->ann_search(query_vector_, limit_ * 2, probes_); // 多取一倍 limit，使粗筛范围更大，防止丢失最优解 在sort算子重新排序后会截断到K
    current_idx_ = 0;

    // RowTuple 需要 schema 才能解析记录字段
    tuple_.set_schema(table_, table_->table_meta().field_metas());

    return RC::SUCCESS;
}

// 下一个元组
RC VectorIndexScanPhysicalOperator::next() {
    // 先校验防止超范围
    if (current_idx_ >= rids_.size()) {
        return RC::RECORD_EOF;
    }

    // 回表，用RID读完整记录
    RC rc = table_->get_record(rids_[current_idx_], current_record_);
    if (rc != RC::SUCCESS) {
        return rc;
    }

    // 记录传入RowTuple
    tuple_.set_record(&current_record_);
    current_idx_++;
    return RC::SUCCESS;
}

// 关闭算子
RC VectorIndexScanPhysicalOperator::close() {
    rids_.clear();
    current_idx_ = 0;
    return RC::SUCCESS;
}

// 返回表的字段数
int VectorIndexScanPhysicalOperator::cell_num() const
{
    return table_->table_meta().field_num();
}

// 记录
Tuple *VectorIndexScanPhysicalOperator::current_tuple()
{
    return &tuple_;
}

// 记录元信息
RC VectorIndexScanPhysicalOperator::tuple_schema(TupleSchema &schema) const
{
    // 从表元数据构建 schema
    const TableMeta &meta = table_->table_meta();
    for (int i = 0; i < meta.field_num(); i++) {
        const FieldMeta *fm = meta.field(i);
        if (fm) {
            schema.append_cell(table_->name(), fm->name());
        }
    }
    return RC::SUCCESS;
}