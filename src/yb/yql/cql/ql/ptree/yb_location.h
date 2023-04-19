//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//
// This module defines an abstract interface for location(line and column) of a token in a SQL
// statement. The location value is to be kept in tree nodes, and when reporting an execution error
// or warning, SQL engine will use this value to indicate where the error occurs.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <stdio.h>

#include "yb/util/memory/mc_types.h"

namespace yb {
namespace ql {

class YBLocation : public MCBase {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<YBLocation> SharedPtr;
  typedef MCSharedPtr<const YBLocation> SharedPtrConst;

  typedef MCUniPtr<YBLocation> UniPtr;
  typedef MCUniPtr<const YBLocation> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  YBLocation() {
  }
  ~YBLocation() {
  }

  //------------------------------------------------------------------------------------------------
  // Abstract interface for location of a token.

  // Line and column at the beginning character of a token.
  virtual int BeginLine() const = 0;
  virtual int BeginColumn() const = 0;

  // Line and column at the ending character of a token.
  virtual int EndLine() const = 0;
  virtual int EndColumn() const = 0;

  // Convert to string for printing messages.
  // In general, we only need to record the starting location of a token where error occurs.
  template<typename StringType>
  void ToString(StringType *msg, bool starting_location_only = true) const {
    char temp[4096];
    if (starting_location_only) {
      snprintf(temp, sizeof(temp), "%d.%d", BeginLine(), BeginColumn());
    } else if (BeginLine() == EndLine()) {
      if (BeginColumn() == EndColumn()) {
        snprintf(temp, sizeof(temp), "%d.%d", BeginLine(), BeginColumn());
      } else {
        snprintf(temp, sizeof(temp), "%d.%d-%d", BeginLine(), BeginColumn(), EndColumn());
      }
    } else {
      snprintf(temp, sizeof(temp), "%d.%d-%d.%d",
               BeginLine(), BeginColumn(), EndLine(), EndColumn());
    }
    *msg += temp;
  }
};

//--------------------------------------------------------------------------------------------------

// Template to print the location as "line#.column#" such as "1.32" for line #1 and column #32.
template <typename YYChar>
inline std::basic_ostream<YYChar>&
operator<< (std::basic_ostream<YYChar>& ostr, const YBLocation& loc) {
  // In general, we only need to record the starting location of a token where error occurs.
  constexpr bool kRecordStartingLocationOnly = true;

  if (kRecordStartingLocationOnly) {
    ostr << loc.BeginLine() << "." << loc.BeginColumn();
  } else if (loc.BeginLine() == loc.EndLine() && loc.BeginColumn() == loc.EndColumn()) {
    ostr << loc.BeginLine() << "." << loc.BeginColumn();
  } else {
    ostr << loc.BeginLine() << "." << loc.BeginColumn()
         << " - " << loc.EndLine() << "." << loc.EndColumn();
  }
  return ostr;
}

}  // namespace ql
}  // namespace yb
