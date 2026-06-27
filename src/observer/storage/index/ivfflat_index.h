/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2.
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include <string>
#include <vector>

#include "storage/index/index.h"

/**
 * @brief IVF_Flat 向量索引
 */
class IvfflatIndex : public Index
{
public:
  IvfflatIndex() = default;
  ~IvfflatIndex() noexcept override;

  RC create(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta) override;
  RC open(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta) override;
  RC close();

  RC insert_entry(const char *record, const RID *rid) override;
  RC delete_entry(const char *record, const RID *rid) override;
  RC sync() override;

  IndexScanner *create_scanner(const char *left_key, int left_len, bool left_inclusive, const char *right_key,
      int right_len, bool right_inclusive) override;

  bool is_vector_index() override { return true; }

  /**
   * @brief 在 probes 个簇中执行近似 Top-K 查询
   */
  RC ann_search(
      const std::vector<float> &query_vector, size_t limit, int probes, bool ascending, std::vector<RID> &result) const;

  bool supports_distance(const std::string &distance_type) const;

private:
  RC do_kmeans(const std::vector<std::vector<float>> &vectors);
  RC load_file(const char *file_name);
  RC write_file(const char *file_name) const;
  RC vector_from_record(const char *record, std::vector<float> &vector) const;

  int   nearest_center(const std::vector<float> &vector) const;
  float vector_distance(const std::vector<float> &left, const std::vector<float> &right) const;

  static RC normalize_distance_type(const std::string &input, std::string &output);

private:
  bool        inited_ = false;
  bool        dirty_  = false;
  Table      *table_  = nullptr;
  std::string file_name_;
  std::string distance_type_ = "l2_distance";
  int         lists_         = 0;
  int         probes_        = 1;
  int         dim_           = 0;

  std::vector<std::vector<float>> centers_;
  std::vector<std::vector<RID>>   inverted_lists_;
};
