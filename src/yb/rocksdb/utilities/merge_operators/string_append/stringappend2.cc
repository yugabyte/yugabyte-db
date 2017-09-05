/**
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

#include "yb/rocksdb/utilities/merge_operators/string_append/stringappend2.h"

#include <assert.h>

#include <memory>
#include <string>

#include "yb/util/slice.h"
#include "yb/rocksdb/merge_operator.h"
#include "yb/rocksdb/utilities/merge_operators.h"

namespace rocksdb {

// Constructor: also specify the delimiter character.
StringAppendTESTOperator::StringAppendTESTOperator(char delim_char)
    : delim_(delim_char) {
}

// Implementation for the merge operation (concatenates two strings)
bool StringAppendTESTOperator::FullMerge(
    const Slice& key,
    const Slice* existing_value,
    const std::deque<std::string>& operands,
    std::string* new_value,
    Logger* logger) const {

  // Clear the *new_value for writing.
  assert(new_value);
  new_value->clear();

  // Compute the space needed for the final result.
  size_t numBytes = 0;
  for(auto it = operands.begin(); it != operands.end(); ++it) {
    numBytes += it->size() + 1;   // Plus 1 for the delimiter
  }

  // Only print the delimiter after the first entry has been printed
  bool printDelim = false;

  // Prepend the *existing_value if one exists.
  if (existing_value) {
    new_value->reserve(numBytes + existing_value->size());
    new_value->append(existing_value->cdata(), existing_value->size());
    printDelim = true;
  } else if (numBytes) {
    new_value->reserve(numBytes-1); // Minus 1 since we have one less delimiter
  }

  // Concatenate the sequence of strings (and add a delimiter between each)
  for(auto it = operands.begin(); it != operands.end(); ++it) {
    if (printDelim) {
      new_value->append(1, delim_);
    }
    new_value->append(*it);
    printDelim = true;
  }

  return true;
}

bool StringAppendTESTOperator::PartialMergeMulti(
    const Slice& key, const std::deque<Slice>& operand_list,
    std::string* new_value, Logger* logger) const {
  return false;
}

// A version of PartialMerge that actually performs "partial merging".
// Use this to simulate the exact behaviour of the StringAppendOperator.
bool StringAppendTESTOperator::_AssocPartialMergeMulti(
    const Slice& key, const std::deque<Slice>& operand_list,
    std::string* new_value, Logger* logger) const {
  // Clear the *new_value for writing
  assert(new_value);
  new_value->clear();
  assert(operand_list.size() >= 2);

  // Generic append
  // Determine and reserve correct size for *new_value.
  size_t size = 0;
  for (const auto& operand : operand_list) {
    size += operand.size();
  }
  size += operand_list.size() - 1;  // Delimiters
  new_value->reserve(size);

  // Apply concatenation
  new_value->assign(operand_list.front().cdata(), operand_list.front().size());

  for (std::deque<Slice>::const_iterator it = operand_list.begin() + 1;
       it != operand_list.end(); ++it) {
    new_value->append(1, delim_);
    new_value->append(it->cdata(), it->size());
  }

  return true;
}

const char* StringAppendTESTOperator::Name() const  {
  return "StringAppendTESTOperator";
}


std::shared_ptr<MergeOperator>
MergeOperators::CreateStringAppendTESTOperator() {
  return std::make_shared<StringAppendTESTOperator>(',');
}

} // namespace rocksdb
