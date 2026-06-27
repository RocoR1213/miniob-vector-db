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
// Created by Wangyunlai on 2021/5/12.
//

#pragma once

#include "common/sys/rc.h"
#include "common/lang/string.h"

class TableMeta;
class FieldMeta;

namespace Json {
class Value;
}  // namespace Json

/**
 * @brief 描述一个索引
 * @ingroup Index
 * @details 一个索引包含了表的哪些字段，索引的名称等。
 * 如果以后实现了多种类型的索引，还需要记录索引的类型，对应类型的一些元数据等
 */
class IndexMeta
{
public:
  IndexMeta() = default;

  RC init(const char *name, const FieldMeta &field);

public:
  const char *name() const;
  const char *field() const;

  bool        is_vector_index() const { return index_type_ == "ivfflat"; }
  const char *index_type() const { return index_type_.c_str(); }
  const char *distance_type() const { return distance_type_.c_str(); }
  int         lists() const { return lists_; }
  int         probes() const { return probes_; }

  void set_index_type(const char *type) { index_type_ = type; }
  void set_distance_type(const char *distance_type) { distance_type_ = distance_type; }
  void set_lists(int lists) { lists_ = lists; }
  void set_probes(int probes) { probes_ = probes; }

  void desc(ostream &os) const;

public:
  void      to_json(Json::Value &json_value) const;
  static RC from_json(const TableMeta &table, const Json::Value &json_value, IndexMeta &index);

protected:
  string name_;   // index's name
  string field_;  // field's name

  string index_type_    = "bplus_tree";
  string distance_type_ = "l2_distance";
  int    lists_         = 0;
  int    probes_        = 0;
};
