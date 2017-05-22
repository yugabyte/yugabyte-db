//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This module defines an abstract interface for location(line and column) of a token in a SQL
// statement. The location value is to be kept in tree nodes, and when reporting an execution error
// or warning, SQL engine will use this value to indicate where the error occurs.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_YB_LOCATION_H_
#define YB_SQL_PTREE_YB_LOCATION_H_

#include "yb/util/memory/mc_types.h"

namespace yb {
namespace sql {

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
  template<typename StringType>
  void ToString(StringType *msg) const {
    // In general, we only need to record the starting location of a token where error occurs.
    constexpr bool kRecordStartingLocationOnly = true;

    char temp[4096];
    if (kRecordStartingLocationOnly) {
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

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_YB_LOCATION_H_
