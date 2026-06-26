/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include "sql/expr/expression.h"
#include "sql/expr/tuple.h"
#include "sql/operator/physical_operator.h"

/**
 * @brief order by 物理算子
 * @ingroup PhysicalOperator
 */
class SortPhysicalOperator : public PhysicalOperator
{
public:
  SortPhysicalOperator(vector<unique_ptr<Expression>> &&expressions, vector<bool> &&ascending);
  virtual ~SortPhysicalOperator() = default;

  PhysicalOperatorType type() const override { return PhysicalOperatorType::SORT; }
  OpType               get_op_type() const override { return OpType::ORDERBY; }

  string name() const override { return "SORT"; }

  RC open(Trx *trx) override;
  RC next() override;
  RC close() override;

  Tuple *current_tuple() override { return &current_tuple_; }
  RC     tuple_schema(TupleSchema &schema) const override;

private:
  struct Row
  {
    vector<Value>         cells;
    vector<TupleCellSpec> specs;
    vector<Value>         sort_keys;
  };

  RC materialize_tuple(const Tuple &tuple, Row &row);
  bool less_than(const Row &left, const Row &right) const;

private:
  vector<unique_ptr<Expression>> order_by_expressions_;
  vector<bool>                   ascending_;
  vector<Row>                    rows_;
  size_t                         cursor_ = 0;
  ValueListTuple                 current_tuple_;
};
