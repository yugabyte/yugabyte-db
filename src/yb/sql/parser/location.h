//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Wrapper for BISON location.hh. This module implements the abstract interface YBLocation, which
// identifies the location of tokens in SQL statement. When generating parse tree, the parser
// (BISON) will save the location values in the tree nodes.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PARSER_LOCATION_H_
#define YB_SQL_PARSER_LOCATION_H_

#include "yb/sql/ptree/yb_location.h"
#include "yb/sql/parser/location.hh"

namespace yb {
namespace sql {

class Location : public YBLocation {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<Location> SharedPtr;
  typedef MCSharedPtr<const Location> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor.
  explicit Location(const location& loc) : loc_(loc) {
  }
  Location(MemoryContext *memctx, const location& loc) : loc_(loc) {
  }
  virtual ~Location() {
  }

  // shared_ptr support.
  template<typename... TypeArgs>
  inline static Location::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<Location>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Abstract interface.
  virtual int BeginLine() const {
    return loc_.begin.line;
  }

  virtual int BeginColumn() const {
    return loc_.begin.column;
  }

  virtual int EndLine() const {
    return loc_.end.line;
  }

  virtual int EndColumn() const {
    return loc_.end.column;
  }

 private:
  location loc_;
};

} // namespace sql
} // namespace yb

#endif // YB_SQL_PARSER_LOCATION_H_
