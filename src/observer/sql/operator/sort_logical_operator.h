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

#include "sql/operator/logical_operator.h"

/**
 * @brief order by 逻辑算子
 * @ingroup LogicalOperator
 */
class SortLogicalOperator : public LogicalOperator
{
public:
  SortLogicalOperator(vector<unique_ptr<Expression>> &&expressions, vector<bool> &&ascending)
      : order_by_expressions_(std::move(expressions)), ascending_(std::move(ascending))
  {}

  virtual ~SortLogicalOperator() = default;

  LogicalOperatorType type() const override { return LogicalOperatorType::SORT; }
  OpType              get_op_type() const override { return OpType::ORDERBY; }

  vector<unique_ptr<Expression>> &order_by_expressions() { return order_by_expressions_; }
  vector<bool>                   &ascending() { return ascending_; }

private:
  vector<unique_ptr<Expression>> order_by_expressions_;
  vector<bool>                   ascending_;
};
