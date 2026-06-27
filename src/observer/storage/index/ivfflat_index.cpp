/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2.
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "storage/index/ivfflat_index.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <unordered_set>

#include "common/lang/string.h"
#include "common/log/log.h"
#include "storage/record/record_scanner.h"
#include "storage/table/table.h"

using namespace std;

namespace {

constexpr uint32_t IVFFLAT_FILE_MAGIC    = 0x31465649;
constexpr uint32_t IVFFLAT_FILE_VERSION  = 1;
constexpr int      MAX_KMEANS_ITERATIONS = 20;
constexpr float    KMEANS_EPSILON        = 1e-4F;

template <typename T>
bool read_binary(ifstream &stream, T &value)
{
  stream.read(reinterpret_cast<char *>(&value), sizeof(T));
  return stream.good();
}

template <typename T>
bool write_binary(ofstream &stream, const T &value)
{
  stream.write(reinterpret_cast<const char *>(&value), sizeof(T));
  return stream.good();
}

bool rid_less(const RID &left, const RID &right) { return RID::compare(&left, &right) < 0; }

}  // namespace

IvfflatIndex::~IvfflatIndex() noexcept
{
  RC rc = close();
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to close IVF_Flat index. index=%s, rc=%s", index_meta_.name(), strrc(rc));
  }
}

RC IvfflatIndex::normalize_distance_type(const string &input, string &output)
{
  output = input;
  common::strip(output);
  common::str_to_lower(output);
  if (output == "euclidean" || output == "l2") {
    output = "l2_distance";
  } else if (output == "cosine") {
    output = "cosine_distance";
  } else if (output == "dot") {
    output = "inner_product";
  }

  if (output != "l2_distance" && output != "cosine_distance" && output != "inner_product") {
    return RC::INVALID_ARGUMENT;
  }
  return RC::SUCCESS;
}

bool IvfflatIndex::supports_distance(const string &distance_type) const
{
  string normalized;
  return normalize_distance_type(distance_type, normalized) == RC::SUCCESS && normalized == distance_type_;
}

RC IvfflatIndex::vector_from_record(const char *record, vector<float> &vector) const
{
  if (record == nullptr || dim_ <= 0) {
    return RC::INVALID_ARGUMENT;
  }

  vector.resize(dim_);
  memcpy(vector.data(), record + field_meta_.offset(), static_cast<size_t>(dim_) * sizeof(float));
  return RC::SUCCESS;
}

float IvfflatIndex::vector_distance(const vector<float> &left, const vector<float> &right) const
{
  double dot        = 0.0;
  double left_norm  = 0.0;
  double right_norm = 0.0;
  double l2_sum     = 0.0;
  for (size_t i = 0; i < left.size(); i++) {
    const double l = left[i];
    const double r = right[i];
    const double d = l - r;
    dot += l * r;
    left_norm += l * l;
    right_norm += r * r;
    l2_sum += d * d;
  }

  if (distance_type_ == "inner_product") {
    return static_cast<float>(dot);
  }
  if (distance_type_ == "cosine_distance") {
    if (left_norm <= 1e-12 || right_norm <= 1e-12) {
      return 1.0F;
    }
    return static_cast<float>(1.0 - dot / (sqrt(left_norm) * sqrt(right_norm)));
  }
  return static_cast<float>(sqrt(l2_sum));
}

int IvfflatIndex::nearest_center(const vector<float> &vector) const
{
  if (centers_.empty()) {
    return -1;
  }

  int   best       = 0;
  float best_score = vector_distance(vector, centers_.front());
  for (size_t i = 1; i < centers_.size(); i++) {
    const float score  = vector_distance(vector, centers_[i]);
    const bool  better = distance_type_ == "inner_product" ? score > best_score : score < best_score;
    if (better) {
      best       = static_cast<int>(i);
      best_score = score;
    }
  }
  return best;
}

RC IvfflatIndex::do_kmeans(const vector<vector<float>> &vectors)
{
  if (vectors.empty() || lists_ <= 0) {
    centers_.clear();
    return RC::SUCCESS;
  }

  vector<size_t> indices(vectors.size());
  iota(indices.begin(), indices.end(), 0);
  mt19937 random_engine(0);
  shuffle(indices.begin(), indices.end(), random_engine);

  centers_.resize(lists_);
  for (int i = 0; i < lists_; i++) {
    centers_[i] = vectors[indices[static_cast<size_t>(i)]];
  }

  vector<int> assignments(vectors.size(), 0);
  for (int iteration = 0; iteration < MAX_KMEANS_ITERATIONS; iteration++) {
    for (size_t i = 0; i < vectors.size(); i++) {
      assignments[i] = nearest_center(vectors[i]);
    }

    vector<vector<float>> new_centers(static_cast<size_t>(lists_), vector<float>(static_cast<size_t>(dim_), 0.0F));
    vector<size_t>        counts(static_cast<size_t>(lists_), 0);
    for (size_t i = 0; i < vectors.size(); i++) {
      const int center = assignments[i];
      counts[center]++;
      for (int dimension = 0; dimension < dim_; dimension++) {
        new_centers[center][dimension] += vectors[i][dimension];
      }
    }

    bool changed = false;
    for (int center = 0; center < lists_; center++) {
      if (counts[center] == 0) {
        new_centers[center] = centers_[center];
      } else {
        for (int dimension = 0; dimension < dim_; dimension++) {
          new_centers[center][dimension] /= static_cast<float>(counts[center]);
        }
      }
      double center_shift = 0.0;
      for (int dimension = 0; dimension < dim_; dimension++) {
        const double difference = centers_[center][dimension] - new_centers[center][dimension];
        center_shift += difference * difference;
      }
      if (sqrt(center_shift) > KMEANS_EPSILON) {
        changed = true;
      }
    }
    centers_.swap(new_centers);
    if (!changed) {
      break;
    }
  }
  return RC::SUCCESS;
}

RC IvfflatIndex::create(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta)
{
  if (inited_) {
    return RC::RECORD_OPENNED;
  }
  if (table == nullptr || common::is_blank(file_name) || !index_meta.is_vector_index() ||
      field_meta.type() != AttrType::VECTORS || index_meta.lists() <= 0 || index_meta.probes() <= 0 ||
      index_meta.probes() > index_meta.lists() || field_meta.len() <= 0 ||
      field_meta.len() % static_cast<int>(sizeof(float)) != 0) {
    return RC::INVALID_ARGUMENT;
  }

  RC rc = normalize_distance_type(index_meta.distance_type(), distance_type_);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  Index::init(index_meta, field_meta);
  table_     = table;
  file_name_ = file_name;
  probes_    = index_meta.probes();
  dim_       = field_meta.len() / static_cast<int>(sizeof(float));

  RecordScanner *raw_scanner = nullptr;
  rc                         = table_->get_record_scanner(raw_scanner, nullptr, ReadWriteMode::READ_ONLY);
  if (rc != RC::SUCCESS) {
    return rc;
  }
  unique_ptr<RecordScanner> scanner(raw_scanner);

  vector<vector<float>> vectors;
  vector<RID>           rids;
  Record                record;
  while ((rc = scanner->next(record)) == RC::SUCCESS) {
    vector<float> vector;
    rc = vector_from_record(record.data(), vector);
    if (rc != RC::SUCCESS) {
      break;
    }
    vectors.emplace_back(std::move(vector));
    rids.push_back(record.rid());
  }

  RC close_rc = scanner->close_scan();
  if (rc == RC::RECORD_EOF) {
    rc = RC::SUCCESS;
  }
  if (rc == RC::SUCCESS && close_rc != RC::SUCCESS) {
    rc = close_rc;
  }
  if (rc != RC::SUCCESS) {
    return rc;
  }

  lists_ = min(index_meta.lists(), static_cast<int>(vectors.size()));
  rc     = do_kmeans(vectors);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  inverted_lists_.assign(static_cast<size_t>(lists_), {});
  for (size_t i = 0; i < vectors.size(); i++) {
    const int center = nearest_center(vectors[i]);
    if (center < 0) {
      return RC::INTERNAL;
    }
    inverted_lists_[center].push_back(rids[i]);
  }

  inited_ = true;
  dirty_  = true;
  rc      = sync();
  if (rc != RC::SUCCESS) {
    inited_ = false;
  }
  return rc;
}

RC IvfflatIndex::open(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta)
{
  if (inited_) {
    return RC::RECORD_OPENNED;
  }
  if (table == nullptr || common::is_blank(file_name) || !index_meta.is_vector_index() ||
      field_meta.type() != AttrType::VECTORS) {
    return RC::INVALID_ARGUMENT;
  }

  RC rc = normalize_distance_type(index_meta.distance_type(), distance_type_);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  Index::init(index_meta, field_meta);
  table_     = table;
  file_name_ = file_name;
  probes_    = index_meta.probes();
  dim_       = field_meta.len() / static_cast<int>(sizeof(float));
  rc         = load_file(file_name);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  inited_ = true;
  dirty_  = false;
  return RC::SUCCESS;
}

RC IvfflatIndex::load_file(const char *file_name)
{
  ifstream stream(file_name, ios::binary);
  if (!stream.is_open()) {
    return RC::IOERR_OPEN;
  }

  uint32_t marker = 0;
  if (!read_binary(stream, marker)) {
    return RC::IOERR_READ;
  }

  bool legacy_format = marker != IVFFLAT_FILE_MAGIC;
  if (legacy_format) {
    stream.clear();
    stream.seekg(0);
  } else {
    uint32_t version = 0;
    if (!read_binary(stream, version) || version != IVFFLAT_FILE_VERSION) {
      return RC::IOERR_READ;
    }
  }

  int stored_lists  = 0;
  int stored_probes = 0;
  int stored_dim    = 0;
  if (!read_binary(stream, stored_lists) || !read_binary(stream, stored_probes) || !read_binary(stream, stored_dim) ||
      stored_lists < 0 || stored_lists > index_meta_.lists() || stored_probes <= 0 || stored_dim != dim_) {
    return RC::IOERR_READ;
  }

  lists_  = stored_lists;
  probes_ = stored_probes;
  centers_.assign(static_cast<size_t>(lists_), vector<float>(static_cast<size_t>(dim_)));
  inverted_lists_.assign(static_cast<size_t>(lists_), {});
  unordered_set<RID, RIDHash> seen_rids;

  for (int center = 0; center < lists_; center++) {
    if (legacy_format) {
      int stored_center_size = 0;
      if (!read_binary(stream, stored_center_size) || stored_center_size != dim_) {
        return RC::IOERR_READ;
      }
    }
    stream.read(reinterpret_cast<char *>(centers_[center].data()), static_cast<streamsize>(dim_) * sizeof(float));
    if (!stream.good()) {
      return RC::IOERR_READ;
    }

    uint32_t list_size = 0;
    if (legacy_format) {
      int legacy_list_size = 0;
      if (!read_binary(stream, legacy_list_size) || legacy_list_size < 0) {
        return RC::IOERR_READ;
      }
      list_size = static_cast<uint32_t>(legacy_list_size);
    } else if (!read_binary(stream, list_size)) {
      return RC::IOERR_READ;
    }

    inverted_lists_[center].reserve(list_size);
    for (uint32_t i = 0; i < list_size; i++) {
      RID rid;
      if (legacy_format) {
        stream.read(reinterpret_cast<char *>(&rid), sizeof(RID));
        if (!stream.good()) {
          return RC::IOERR_READ;
        }
      } else if (!read_binary(stream, rid.page_num) || !read_binary(stream, rid.slot_num)) {
        return RC::IOERR_READ;
      }
      if (seen_rids.insert(rid).second) {
        inverted_lists_[center].push_back(rid);
      }
    }
  }
  return RC::SUCCESS;
}

RC IvfflatIndex::write_file(const char *file_name) const
{
  ofstream stream(file_name, ios::binary | ios::trunc);
  if (!stream.is_open()) {
    return RC::IOERR_OPEN;
  }

  if (!write_binary(stream, IVFFLAT_FILE_MAGIC) || !write_binary(stream, IVFFLAT_FILE_VERSION) ||
      !write_binary(stream, lists_) || !write_binary(stream, probes_) || !write_binary(stream, dim_)) {
    return RC::IOERR_WRITE;
  }

  for (int center = 0; center < lists_; center++) {
    stream.write(reinterpret_cast<const char *>(centers_[center].data()),
        static_cast<streamsize>(centers_[center].size() * sizeof(float)));
    const uint32_t list_size = static_cast<uint32_t>(inverted_lists_[center].size());
    if (!stream.good() || !write_binary(stream, list_size)) {
      return RC::IOERR_WRITE;
    }
    for (const RID &rid : inverted_lists_[center]) {
      if (!write_binary(stream, rid.page_num) || !write_binary(stream, rid.slot_num)) {
        return RC::IOERR_WRITE;
      }
    }
  }

  stream.flush();
  return stream.good() ? RC::SUCCESS : RC::IOERR_WRITE;
}

RC IvfflatIndex::insert_entry(const char *record, const RID *rid)
{
  if (!inited_ || record == nullptr || rid == nullptr) {
    return RC::INVALID_ARGUMENT;
  }

  vector<float> vector;
  RC            rc = vector_from_record(record, vector);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  if (centers_.empty()) {
    centers_.push_back(vector);
    inverted_lists_.emplace_back();
    lists_ = 1;
  }

  const int center = nearest_center(vector);
  if (center < 0) {
    return RC::INTERNAL;
  }
  inverted_lists_[center].push_back(*rid);
  dirty_ = true;
  return RC::SUCCESS;
}

RC IvfflatIndex::delete_entry(const char *record, const RID *rid)
{
  (void)record;
  if (!inited_ || rid == nullptr) {
    return RC::INVALID_ARGUMENT;
  }

  bool removed = false;
  for (vector<RID> &list : inverted_lists_) {
    const auto new_end = remove(list.begin(), list.end(), *rid);
    if (new_end != list.end()) {
      list.erase(new_end, list.end());
      removed = true;
    }
  }
  if (!removed) {
    return RC::RECORD_INVALID_KEY;
  }

  dirty_ = true;
  return RC::SUCCESS;
}

RC IvfflatIndex::sync()
{
  if (!inited_ || !dirty_) {
    return RC::SUCCESS;
  }

  const string temp_file = file_name_ + ".tmp";
  RC           rc        = write_file(temp_file.c_str());
  if (rc != RC::SUCCESS) {
    ::remove(temp_file.c_str());
    return rc;
  }
  if (rename(temp_file.c_str(), file_name_.c_str()) != 0) {
    ::remove(temp_file.c_str());
    return RC::IOERR_SYNC;
  }

  dirty_ = false;
  return RC::SUCCESS;
}

RC IvfflatIndex::close()
{
  RC rc   = sync();
  inited_ = false;
  return rc;
}

IndexScanner *IvfflatIndex::create_scanner(
    const char *left_key, int left_len, bool left_inclusive, const char *right_key, int right_len, bool right_inclusive)
{
  (void)left_key;
  (void)left_len;
  (void)left_inclusive;
  (void)right_key;
  (void)right_len;
  (void)right_inclusive;
  return nullptr;
}

RC IvfflatIndex::ann_search(
    const vector<float> &query_vector, size_t limit, int probes, bool ascending, vector<RID> &result) const
{
  result.clear();
  if (!inited_ || query_vector.size() != static_cast<size_t>(dim_) || probes <= 0) {
    return RC::INVALID_ARGUMENT;
  }
  if (limit == 0 || centers_.empty()) {
    return RC::SUCCESS;
  }

  vector<pair<float, int>> center_scores;
  center_scores.reserve(centers_.size());
  for (size_t center = 0; center < centers_.size(); center++) {
    center_scores.emplace_back(vector_distance(query_vector, centers_[center]), static_cast<int>(center));
  }
  stable_sort(center_scores.begin(), center_scores.end(), [ascending](const auto &left, const auto &right) {
    if (left.first == right.first) {
      return left.second < right.second;
    }
    return ascending ? left.first < right.first : left.first > right.first;
  });

  const size_t                actual_probes = min(static_cast<size_t>(probes), center_scores.size());
  vector<pair<float, RID>>    candidates;
  unordered_set<RID, RIDHash> seen_rids;
  for (size_t i = 0; i < actual_probes; i++) {
    const int center = center_scores[i].second;
    for (const RID &rid : inverted_lists_[center]) {
      if (!seen_rids.insert(rid).second) {
        continue;
      }

      Record record;
      RC     rc = table_->get_record(rid, record);
      if (rc == RC::RECORD_NOT_EXIST || rc == RC::RECORD_INVALID_RID) {
        continue;
      }
      if (rc != RC::SUCCESS) {
        return rc;
      }

      vector<float> vector;
      rc = vector_from_record(record.data(), vector);
      if (rc != RC::SUCCESS) {
        return rc;
      }
      candidates.emplace_back(vector_distance(query_vector, vector), rid);
    }
  }

  stable_sort(candidates.begin(), candidates.end(), [ascending](const auto &left, const auto &right) {
    if (left.first == right.first) {
      return rid_less(left.second, right.second);
    }
    return ascending ? left.first < right.first : left.first > right.first;
  });

  const size_t result_size = min(limit, candidates.size());
  result.reserve(result_size);
  for (size_t i = 0; i < result_size; i++) {
    result.push_back(candidates[i].second);
  }
  return RC::SUCCESS;
}
