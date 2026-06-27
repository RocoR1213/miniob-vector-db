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
// Created by Wangyunlai.wyl on 2021/5/18.
//

#include "storage/index/index_meta.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "storage/field/field_meta.h"
#include "storage/table/table_meta.h"
#include "json/json.h"

const static Json::StaticString FIELD_NAME("name");
const static Json::StaticString FIELD_FIELD_NAME("field_name");

const static Json::StaticString FIELD_INDEX_TYPE("index_type");
const static Json::StaticString FIELD_DISTANCE_TYPE("distance_type");
const static Json::StaticString FIELD_LISTS("lists");
const static Json::StaticString FIELD_PROBES("probes");

RC IndexMeta::init(const char *name, const FieldMeta &field)
{
  if (common::is_blank(name)) {
    LOG_ERROR("Failed to init index, name is empty.");
    return RC::INVALID_ARGUMENT;
  }

  name_  = name;
  field_ = field.name();
  return RC::SUCCESS;
}

void IndexMeta::to_json(Json::Value &json_value) const
{
  json_value[FIELD_NAME]       = name_;
  json_value[FIELD_FIELD_NAME] = field_;
  json_value[FIELD_INDEX_TYPE] = index_type_;
  if (is_vector_index()) {
    json_value[FIELD_DISTANCE_TYPE] = distance_type_;
    json_value[FIELD_LISTS]         = lists_;
    json_value[FIELD_PROBES]        = probes_;
  }
}

RC IndexMeta::from_json(const TableMeta &table, const Json::Value &json_value, IndexMeta &index)
{
  const Json::Value &name_value  = json_value[FIELD_NAME];
  const Json::Value &field_value = json_value[FIELD_FIELD_NAME];
  if (!name_value.isString()) {
    LOG_ERROR("Index name is not a string. json value=%s", name_value.toStyledString().c_str());
    return RC::INTERNAL;
  }

  if (!field_value.isString()) {
    LOG_ERROR("Field name of index [%s] is not a string. json value=%s",
        name_value.asCString(), field_value.toStyledString().c_str());
    return RC::INTERNAL;
  }

  const FieldMeta *field = table.field(field_value.asCString());
  if (nullptr == field) {
    LOG_ERROR("Deserialize index [%s]: no such field: %s", name_value.asCString(), field_value.asCString());
    return RC::SCHEMA_FIELD_MISSING;
  }

  if (json_value.isMember(FIELD_INDEX_TYPE)) {
    if (!json_value[FIELD_INDEX_TYPE].isString()) {
      LOG_ERROR("Index type of index [%s] is not a string", name_value.asCString());
      return RC::INTERNAL;
    }
    index.index_type_ = json_value[FIELD_INDEX_TYPE].asString();
  }
  if (json_value.isMember(FIELD_DISTANCE_TYPE) && json_value[FIELD_DISTANCE_TYPE].isString()) {
    index.distance_type_ = json_value[FIELD_DISTANCE_TYPE].asString();
  }
  if (json_value.isMember(FIELD_LISTS) && json_value[FIELD_LISTS].isInt()) {
    index.lists_ = json_value[FIELD_LISTS].asInt();
  }
  if (json_value.isMember(FIELD_PROBES) && json_value[FIELD_PROBES].isInt()) {
    index.probes_ = json_value[FIELD_PROBES].asInt();
  }

  // 兼容早期 A4 元数据：当时没有 index_type，仅以 lists/probes 标识向量索引。
  if (!json_value.isMember(FIELD_INDEX_TYPE) && index.lists_ > 0 && index.probes_ > 0) {
    index.index_type_ = "ivfflat";
  }

  return index.init(name_value.asCString(), *field);
}

const char *IndexMeta::name() const { return name_.c_str(); }

const char *IndexMeta::field() const { return field_.c_str(); }

void IndexMeta::desc(ostream &os) const
{
  os << "index name=" << name_ << ", field=" << field_ << ", type=" << index_type_;
  if (is_vector_index()) {
    os << ", distance=" << distance_type_ << ", lists=" << lists_ << ", probes=" << probes_;
  }
}
