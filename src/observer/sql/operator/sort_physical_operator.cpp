/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/operator/sort_physical_operator.h"

#include <algorithm>

#include "common/log/log.h"

using namespace std;

SortPhysicalOperator::SortPhysicalOperator(vector<unique_ptr<Expression>> &&expressions, vector<bool> &&ascending)
    : order_by_expressions_(std::move(expressions)), ascending_(std::move(ascending))
{}

RC SortPhysicalOperator::open(Trx *trx)
{
  if (children_.size() != 1 || order_by_expressions_.size() != ascending_.size()) {
    LOG_WARN("invalid sort operator state. child=%d, expression=%d, ascending=%d",
        children_.size(), order_by_expressions_.size(), ascending_.size());
    return RC::INTERNAL;
  }

  rows_.clear();
  cursor_ = 0;

  PhysicalOperator &child = *children_[0];
  RC                rc    = child.open(trx);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to open sort child. rc=%s", strrc(rc));
    return rc;
  }

  while (OB_SUCC(rc = child.next())) {
    Tuple *tuple = child.current_tuple();
    if (nullptr == tuple) {
      LOG_WARN("failed to get tuple from sort child");
      return RC::INTERNAL;
    }

    Row row;
    rc = materialize_tuple(*tuple, row);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to materialize tuple for sort. rc=%s", strrc(rc));
      return rc;
    }

    rows_.emplace_back(std::move(row));
  }

  if (rc == RC::RECORD_EOF) {
    rc = RC::SUCCESS;
  }
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to read tuples for sort. rc=%s", strrc(rc));
    return rc;
  }

  stable_sort(rows_.begin(), rows_.end(), [this](const Row &left, const Row &right) {
    return less_than(left, right);
  });

  return RC::SUCCESS;
}

RC SortPhysicalOperator::next()
{
  if (cursor_ >= rows_.size()) {
    return RC::RECORD_EOF;
  }

  Row &row = rows_[cursor_++];
  current_tuple_.set_names(row.specs);
  current_tuple_.set_cells(row.cells);
  return RC::SUCCESS;
}

RC SortPhysicalOperator::close()
{
  rows_.clear();
  cursor_ = 0;
  if (!children_.empty()) {
    children_[0]->close();
  }
  return RC::SUCCESS;
}

RC SortPhysicalOperator::tuple_schema(TupleSchema &schema) const
{
  if (children_.size() != 1) {
    return RC::INTERNAL;
  }
  return children_[0]->tuple_schema(schema);
}

RC SortPhysicalOperator::materialize_tuple(const Tuple &tuple, Row &row)
{
  row.sort_keys.reserve(order_by_expressions_.size());
  for (const unique_ptr<Expression> &expression : order_by_expressions_) {
    Value key;
    RC    rc = expression->get_value(tuple, key);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to calculate sort key. rc=%s", strrc(rc));
      return rc;
    }
    if (key.attr_type() == AttrType::VECTORS) {
      LOG_WARN("vector value cannot be used as a sort key");
      return RC::SCHEMA_FIELD_TYPE_MISMATCH;
    }
    row.sort_keys.emplace_back(std::move(key));
  }

  const int cell_num = tuple.cell_num();
  row.cells.reserve(cell_num);
  row.specs.reserve(cell_num);
  for (int i = 0; i < cell_num; i++) {
    Value cell;
    RC    rc = tuple.cell_at(i, cell);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to copy tuple cell. rc=%s", strrc(rc));
      return rc;
    }

    TupleCellSpec spec;
    rc = tuple.spec_at(i, spec);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to copy tuple spec. rc=%s", strrc(rc));
      return rc;
    }

    row.cells.emplace_back(std::move(cell));
    row.specs.emplace_back(std::move(spec));
  }

  return RC::SUCCESS;
}

bool SortPhysicalOperator::less_than(const Row &left, const Row &right) const
{
  for (size_t i = 0; i < order_by_expressions_.size(); i++) {
    int cmp_result = left.sort_keys[i].compare(right.sort_keys[i]);
    if (cmp_result == 0) {
      continue;
    }

    return ascending_[i] ? cmp_result < 0 : cmp_result > 0;
  }

  return false;
}
