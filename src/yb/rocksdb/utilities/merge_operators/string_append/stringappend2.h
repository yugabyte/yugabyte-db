/**
 * A TEST MergeOperator for rocksdb that implements string append.
 * It is built using the MergeOperator interface rather than the simpler
 * AssociativeMergeOperator interface. This is useful for testing/benchmarking.
 * While the two operators are semantically the same, all production code
 * should use the StringAppendOperator defined in stringappend.{h,cc}. The
 * operator defined in the present file is primarily for testing.
 *
 * @author Deon Nicholas (dnicholas@fb.com)
 * Copyright 2013 Facebook
 *
 * The following only applies to changes made to this file as part of YugaByte development.
 *
 * Portions Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 */

#pragma once
#include <deque>
#include <string>

#include "yb/rocksdb/merge_operator.h"
#include "yb/util/slice.h"

namespace rocksdb {

class StringAppendTESTOperator : public MergeOperator {
 public:
  // Constructor with delimiter
  explicit StringAppendTESTOperator(char delim_char);

  virtual bool FullMerge(const Slice& key,
                         const Slice* existing_value,
                         const std::deque<std::string>& operand_sequence,
                         std::string* new_value,
                         Logger* logger) const override;

  virtual bool PartialMergeMulti(const Slice& key,
                                 const std::deque<Slice>& operand_list,
                                 std::string* new_value, Logger* logger) const
      override;

  virtual const char* Name() const override;

 private:
  // A version of PartialMerge that actually performs "partial merging".
  // Use this to simulate the exact behaviour of the StringAppendOperator.
  bool _AssocPartialMergeMulti(const Slice& key,
                               const std::deque<Slice>& operand_list,
                               std::string* new_value, Logger* logger) const;

  char delim_;         // The delimiter is inserted between elements

};

} // namespace rocksdb
