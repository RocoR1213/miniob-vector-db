/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/operator/limit_physical_operator.h"

#include "common/log/log.h"

RC LimitPhysicalOperator::open(Trx *trx)
{
  if (children_.size() != 1) {
    LOG_WARN("limit operator should have one child, but got %d", children_.size());
    return RC::INTERNAL;
  }

  emitted_ = 0;
  return children_[0]->open(trx);
}

RC LimitPhysicalOperator::next()
{
  if (limit_ >= 0 && emitted_ >= limit_) {
    return RC::RECORD_EOF;
  }

  RC rc = children_[0]->next();
  if (OB_SUCC(rc)) {
    emitted_++;
  }
  return rc;
}

RC LimitPhysicalOperator::close()
{
  if (!children_.empty()) {
    children_[0]->close();
  }
  return RC::SUCCESS;
}

Tuple *LimitPhysicalOperator::current_tuple()
{
  if (children_.empty()) {
    return nullptr;
  }
  return children_[0]->current_tuple();
}

RC LimitPhysicalOperator::tuple_schema(TupleSchema &schema) const
{
  if (children_.size() != 1) {
    return RC::INTERNAL;
  }
  return children_[0]->tuple_schema(schema);
}
