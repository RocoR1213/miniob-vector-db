/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Wangyunlai on 2022/6/6.
//

#include "sql/stmt/select_stmt.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "sql/stmt/filter_stmt.h"
#include "storage/db/db.h"
#include "storage/table/table.h"
#include "sql/parser/expression_binder.h"

using namespace std;
using namespace common;

SelectStmt::~SelectStmt()
{
  if (nullptr != filter_stmt_) {
    delete filter_stmt_;
    filter_stmt_ = nullptr;
  }
}

static bool is_order_by_alias_reference(const Expression &expression, const char *&alias_name)
{
  if (expression.type() != ExprType::UNBOUND_FIELD) {
    return false;
  }

  const auto *field_expr = static_cast<const UnboundFieldExpr *>(&expression);
  if (!is_blank(field_expr->table_name())) {
    return false;
  }

  alias_name = field_expr->field_name();
  return !is_blank(alias_name);
}

static unique_ptr<Expression> find_order_by_alias_expression(
    const char *alias_name, const vector<unique_ptr<Expression>> &select_expressions)
{
  for (const unique_ptr<Expression> &expression : select_expressions) {
    if (!is_blank(expression->name()) && 0 == strcasecmp(alias_name, expression->name())) {
      return expression->copy();
    }
  }

  return nullptr;
}

static bool is_sortable_type(AttrType type)
{
  return type == AttrType::INTS || type == AttrType::FLOATS || type == AttrType::CHARS ||
         type == AttrType::BOOLEANS;
}

static RC bind_order_by_expressions(ExpressionBinder &expression_binder,
    vector<OrderBySqlNode> &order_by_nodes, const vector<unique_ptr<Expression>> &select_expressions,
    vector<unique_ptr<Expression>> &order_by_expressions, vector<bool> &order_by_ascending)
{
  for (OrderBySqlNode &order_by_node : order_by_nodes) {
    unique_ptr<Expression> order_by_expression;

    const char *alias_name = nullptr;
    if (order_by_node.expression &&
        is_order_by_alias_reference(*order_by_node.expression, alias_name)) {
      order_by_expression = find_order_by_alias_expression(alias_name, select_expressions);
    }

    if (!order_by_expression) {
      vector<unique_ptr<Expression>> bound_order_by;
      RC rc = expression_binder.bind_expression(order_by_node.expression, bound_order_by);
      if (OB_FAIL(rc)) {
        LOG_INFO("bind order by expression failed. rc=%s", strrc(rc));
        return rc;
      }
      if (bound_order_by.size() != 1) {
        LOG_WARN("invalid order by expression number: %d", bound_order_by.size());
        return RC::INVALID_ARGUMENT;
      }
      order_by_expression = std::move(bound_order_by[0]);
    }

    if (!is_sortable_type(order_by_expression->value_type())) {
      LOG_WARN("unsupported order by type: %s", attr_type_to_string(order_by_expression->value_type()));
      return RC::SCHEMA_FIELD_TYPE_MISMATCH;
    }

    order_by_expressions.emplace_back(std::move(order_by_expression));
    order_by_ascending.emplace_back(order_by_node.asc);
  }

  return RC::SUCCESS;
}

RC SelectStmt::create(Db *db, SelectSqlNode &select_sql, Stmt *&stmt)
{
  if (nullptr == db) {
    LOG_WARN("invalid argument. db is null");
    return RC::INVALID_ARGUMENT;
  }

  BinderContext binder_context;

  // collect tables in `from` statement
  vector<Table *>                tables;
  unordered_map<string, Table *> table_map;
  for (size_t i = 0; i < select_sql.relations.size(); i++) {
    const char *table_name = select_sql.relations[i].c_str();
    if (nullptr == table_name) {
      LOG_WARN("invalid argument. relation name is null. index=%d", i);
      return RC::INVALID_ARGUMENT;
    }

    Table *table = db->find_table(table_name);
    if (nullptr == table) {
      LOG_WARN("no such table. db=%s, table_name=%s", db->name(), table_name);
      return RC::SCHEMA_TABLE_NOT_EXIST;
    }

    binder_context.add_table(table);
    tables.push_back(table);
    table_map.insert({table_name, table});
  }

  // collect query fields in `select` statement
  vector<unique_ptr<Expression>> bound_expressions;
  ExpressionBinder expression_binder(binder_context);
  
  for (unique_ptr<Expression> &expression : select_sql.expressions) {
    RC rc = expression_binder.bind_expression(expression, bound_expressions);
    if (OB_FAIL(rc)) {
      LOG_INFO("bind expression failed. rc=%s", strrc(rc));
      return rc;
    }
  }

  vector<unique_ptr<Expression>> group_by_expressions;
  for (unique_ptr<Expression> &expression : select_sql.group_by) {
    RC rc = expression_binder.bind_expression(expression, group_by_expressions);
    if (OB_FAIL(rc)) {
      LOG_INFO("bind expression failed. rc=%s", strrc(rc));
      return rc;
    }
  }

  vector<unique_ptr<Expression>> order_by_expressions;
  vector<bool>                   order_by_ascending;
  RC rc = bind_order_by_expressions(
      expression_binder, select_sql.order_by, bound_expressions, order_by_expressions, order_by_ascending);
  if (OB_FAIL(rc)) {
    return rc;
  }

  Table *default_table = nullptr;
  if (tables.size() == 1) {
    default_table = tables[0];
  }

  // create filter statement in `where` statement
  FilterStmt *filter_stmt = nullptr;
  rc                      = FilterStmt::create(db,
      default_table,
      &table_map,
      select_sql.conditions.data(),
      static_cast<int>(select_sql.conditions.size()),
      filter_stmt);
  if (rc != RC::SUCCESS) {
    LOG_WARN("cannot construct filter stmt");
    return rc;
  }

  // everything alright
  SelectStmt *select_stmt = new SelectStmt();

  select_stmt->tables_.swap(tables);
  select_stmt->query_expressions_.swap(bound_expressions);
  select_stmt->filter_stmt_ = filter_stmt;
  select_stmt->group_by_.swap(group_by_expressions);
  select_stmt->order_by_.swap(order_by_expressions);
  select_stmt->order_by_ascending_.swap(order_by_ascending);
  select_stmt->limit_ = select_sql.limit;
  stmt                      = select_stmt;
  return RC::SUCCESS;
}
