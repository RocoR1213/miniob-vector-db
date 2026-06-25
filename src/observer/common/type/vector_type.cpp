/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "common/type/vector_type.h"

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "common/lang/sstream.h"
#include "common/lang/string.h"
#include "common/lang/vector.h"
#include "common/log/log.h"
#include "common/value.h"

int VectorType::compare(const Value &left, const Value &right) const
{
  ASSERT(left.attr_type() == AttrType::VECTORS && right.attr_type() == AttrType::VECTORS, "invalid type");
  if (left.length_ != right.length_) {
    return INT32_MAX;
  }
  return memcmp(left.value_.pointer_value_, right.value_.pointer_value_, left.length_);
}

RC VectorType::set_value_from_str(Value &val, const string &data) const
{
  string text;
  text.reserve(data.size());
  for (char c : data) {
    if (!isspace(static_cast<unsigned char>(c))) {
      text.push_back(c);
    }
  }

  if (text.size() < 3 || text.front() != '[' || text.back() != ']') {
    LOG_WARN("invalid vector literal format: %s", data.c_str());
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  string inner = text.substr(1, text.size() - 2);
  if (inner.empty()) {
    LOG_WARN("empty vector literal is not allowed");
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  vector<float> values;
  size_t        start = 0;
  while (start <= inner.size()) {
    size_t end = inner.find(',', start);
    string item = end == string::npos ? inner.substr(start) : inner.substr(start, end - start);
    if (item.empty()) {
      LOG_WARN("empty vector item is not allowed");
      return RC::SCHEMA_FIELD_TYPE_MISMATCH;
    }

    errno       = 0;
    char *tail  = nullptr;
    float value = strtof(item.c_str(), &tail);
    if (tail == item.c_str() || *tail != '\0' || errno == ERANGE || !std::isfinite(value)) {
      LOG_WARN("invalid vector item: %s", item.c_str());
      return RC::SCHEMA_FIELD_TYPE_MISMATCH;
    }
    values.push_back(value);

    if (end == string::npos) {
      break;
    }
    start = end + 1;
  }

  if (values.empty()) {
    LOG_WARN("empty vector literal is not allowed");
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  val.set_vector(reinterpret_cast<const char *>(values.data()), static_cast<int>(values.size() * sizeof(float)));
  return RC::SUCCESS;
}

RC VectorType::to_string(const Value &val, string &result) const
{
  if (val.attr_type() != AttrType::VECTORS || val.length_ <= 0 || val.length_ % sizeof(float) != 0) {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  const int    dimension = val.length_ / static_cast<int>(sizeof(float));
  const float *values    = reinterpret_cast<const float *>(val.value_.pointer_value_);
  stringstream ss;
  ss << "[";
  for (int i = 0; i < dimension; i++) {
    if (i > 0) {
      ss << ",";
    }
    ss << common::double_to_str(values[i]);
  }
  ss << "]";
  result = ss.str();
  return RC::SUCCESS;
}
