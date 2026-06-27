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
// Created by Wangyunlai on 2023/4/25.
//

// Panda 语义对象定义 裸数据将通过工厂方法create转换为index对象
// A4 新增index_type区分索引类型，新增向量类型索引和lists和probes字段

#pragma once

#include "sql/stmt/stmt.h"

struct CreateIndexSqlNode;
class Table;
class FieldMeta;

/**
 * @brief 创建索引的语句
 * @ingroup Statement
 */
class CreateIndexStmt : public Stmt
{
public:
  // A4 构造函数新增三项接收数据
  CreateIndexStmt(Table *table, const FieldMeta *field_meta, const string &index_name,
                  const string &index_type = "", int lists = 0, int probes = 0)
      : table_(table), field_meta_(field_meta), index_name_(index_name),
        index_type_(index_type), lists_(lists), probes_(probes)
  {}

  virtual ~CreateIndexStmt() = default;

  StmtType type() const override { return StmtType::CREATE_INDEX; }

  Table           *table() const { return table_; }
  const FieldMeta *field_meta() const { return field_meta_; }
  const string    &index_name() const { return index_name_; }

  // A4 三项新增数据getter
  const string    &index_type() const { return index_type_; }
  int               lists() const { return lists_; }
  int               probes() const { return probes_; }

public:
  static RC create(Db *db, const CreateIndexSqlNode &create_index, Stmt *&stmt);

private:
  Table           *table_      = nullptr;
  const FieldMeta *field_meta_ = nullptr;
  string           index_name_;

  // A4 三项新增数据
  string           index_type_;
  int              lists_ = 0;
  int              probes_ = 0;
};
