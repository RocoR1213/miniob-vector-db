/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2.
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/operator/vector_index_scan_physical_operator.h"

#include "storage/index/ivfflat_index.h"
#include "storage/table/table.h"

using namespace std;

VectorIndexScanPhysicalOperator::VectorIndexScanPhysicalOperator(
    Table *table, IvfflatIndex *index, const vector<float> &query_vector, int limit, int probes, bool ascending)
    : table_(table), index_(index), query_vector_(query_vector), limit_(limit), probes_(probes), ascending_(ascending)
{}

string VectorIndexScanPhysicalOperator::param() const { return index_ == nullptr ? "" : index_->index_meta().name(); }

RC VectorIndexScanPhysicalOperator::open(Trx *trx)
{
  (void)trx;
  if (table_ == nullptr || index_ == nullptr || limit_ < 0 || probes_ <= 0) {
    return RC::INVALID_ARGUMENT;
  }

  RC rc = index_->ann_search(query_vector_, static_cast<size_t>(limit_), probes_, ascending_, rids_);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  current_idx_ = 0;
  tuple_.set_schema(table_, table_->table_meta().field_metas());
  return RC::SUCCESS;
}

RC VectorIndexScanPhysicalOperator::next()
{
  if (current_idx_ >= rids_.size()) {
    return RC::RECORD_EOF;
  }

  RC rc = table_->get_record(rids_[current_idx_], current_record_);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  current_idx_++;
  tuple_.set_record(&current_record_);
  return RC::SUCCESS;
}

RC VectorIndexScanPhysicalOperator::close()
{
  rids_.clear();
  current_idx_ = 0;
  return RC::SUCCESS;
}

Tuple *VectorIndexScanPhysicalOperator::current_tuple() { return &tuple_; }

RC VectorIndexScanPhysicalOperator::tuple_schema(TupleSchema &schema) const
{
  const TableMeta &table_meta = table_->table_meta();
  for (int i = 0; i < table_meta.field_num(); i++) {
    const FieldMeta *field_meta = table_meta.field(i);
    if (field_meta != nullptr) {
      schema.append_cell(table_->name(), field_meta->name());
    }
  }
  return RC::SUCCESS;
}
