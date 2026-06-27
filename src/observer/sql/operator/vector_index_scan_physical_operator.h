/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

// A4 向量索引扫描物理算子

#pragma once

#include "sql/operator/physical_operator.h"
#include "sql/expr/tuple.h"
#include "storage/record/record.h"
#include <vector>

class Table;
class IvfflatIndex;

using namespace std;

class VectorIndexScanPhysicalOperator : public PhysicalOperator {
public:
    // 构造函数
    VectorIndexScanPhysicalOperator(Table *table, IvfflatIndex *index, const vector<float> &query_vector, int limit, int probes);

    // 析构函数
    ~VectorIndexScanPhysicalOperator() = default;

    // 类型
    PhysicalOperatorType type() const override { return PhysicalOperatorType::VECTOR_INDEX_SCAN; }

    // 打开算子
    RC open(Trx *trx) override;

    // 下一个元组
    RC next() override;

    // 关闭算子
    RC close() override;

    // 表的字段数
    int cell_num() const;

    // 记录
    Tuple *current_tuple() override;
    
    // 记录元信息
    RC tuple_schema(TupleSchema &schema) const override;

private:
    Table           *table_ = nullptr;
    IvfflatIndex    *index_ = nullptr;
    vector<float>   query_vector_;
    int             limit_  = 0;
    int             probes_ = 0;

    vector<RID>     rids_;              // ann_search 返回的候选 RID
    int             current_idx_ = 0;   // 当前遍历位置
    Record          current_record_;    // 当前记录的数据
    RowTuple        tuple_;             // 当前行元数据 防止tuple指向临时对象 出现奇怪的bug
};
