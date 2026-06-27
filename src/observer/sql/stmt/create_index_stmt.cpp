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

#include "sql/stmt/create_index_stmt.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "storage/db/db.h"
#include "storage/table/table.h"

using namespace std;
using namespace common;

/* Panda
原始的create方法大致结构为 找表 -> 找字段 -> 检查重名 -> 构造stmt对象
对于向量类型索引，首先要加上新的参数传入*/

RC CreateIndexStmt::create(Db *db, const CreateIndexSqlNode &create_index, Stmt *&stmt)
{
  // 函数失败时先置空 防御性编程
  stmt = nullptr;

  // 检查所有参数字段是否为空，有一个为空就报错返回日志
  const char *table_name = create_index.relation_name.c_str();
  if (is_blank(table_name) || is_blank(create_index.index_name.c_str()) ||
      is_blank(create_index.attribute_name.c_str())) {
    LOG_WARN("invalid argument. db=%p, table_name=%p, index name=%s, attribute name=%s",
        db, table_name, create_index.index_name.c_str(), create_index.attribute_name.c_str());
    return RC::INVALID_ARGUMENT;
  }

  // 在数据库里查表的存在性
  Table *table = db->find_table(table_name);
  if (nullptr == table) {
    LOG_WARN("no such table. db=%s, table_name=%s", db->name(), table_name);
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  // 在表里找目标字段(列)的存在性
  const FieldMeta *field_meta = table->table_meta().field(create_index.attribute_name.c_str());
  if (nullptr == field_meta) {
    LOG_WARN("no such field in table. db=%s, table=%s, field name=%s", 
             db->name(), table_name, create_index.attribute_name.c_str());
    return RC::SCHEMA_FIELD_NOT_EXIST;
  }

  // 找是否有同名索引
  Index *index = table->find_index(create_index.index_name.c_str());
  if (nullptr != index) {
    LOG_WARN("index with name(%s) already exists. table name=%s", create_index.index_name.c_str(), table_name);
    return RC::SCHEMA_INDEX_NAME_REPEAT;
  }

  // A4 向量索引参数校验
  if (!create_index.index_type.empty()) {
    // tmp 只支持ivfflat类型 非该类型报错
    if (create_index.index_type != "ivfflat") {
      LOG_WARN("unsupported index type: %s", create_index.index_type.c_str());
      return RC::INVALID_ARGUMENT;
    }
    // 检查lists参数
    if (create_index.lists <= 0) {
      LOG_WARN("lists must be positive, got %d", create_index.lists);
      return RC::INVALID_ARGUMENT;
    }
    // 检查probes参数
    if (create_index.probes <= 0) {
      LOG_WARN("probes must be positive, got %d", create_index.probes);
      return RC::INVALID_ARGUMENT;
    }
  }

  // 创建stmt对象
  // A4 新增传入参数
  stmt = new CreateIndexStmt(table, field_meta, create_index.index_name, create_index.index_type, create_index.lists, create_index.probes);
  return RC::SUCCESS;
}
