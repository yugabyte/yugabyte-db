//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include <stdio.h>

#include "yb/sql/parser/parser.h"
#include "yb/sql/parser/parse_context.h"
#include "yb/util/logging.h"

namespace yb {
namespace sql {

using std::min;
using std::istream;

//--------------------------------------------------------------------------------------------------
// ParseContext
//--------------------------------------------------------------------------------------------------

ParseContext::ParseContext(const string& sql_stmt,
                           MemoryContext *parse_mem,
                           MemoryContext *ptree_mem)
    : parse_mem_(parse_mem),
      ptree_mem_(ptree_mem),
      stmt_(parse_mem->Strdup(sql_stmt.c_str())),
      stmt_len_(sql_stmt.size()),
      stmt_offset_(0),
      trace_scanning_(false),
      trace_parsing_(false) {


  // The MAC version of flex requires empty or valid input stream, but the centos version of FLEX
  // require nullptr or valid istream.
#if defined(__APPLE__) || defined(__APPLE) || defined(APPLE)
  sql_file_ = std::unique_ptr<istream>(new istream(nullptr));
#else
  sql_file_ = nullptr;
#endif

  if (VLOG_IS_ON(3)) {
    trace_scanning_ = true;
    trace_parsing_ = true;
  }
}

ParseContext::~ParseContext() {
}

size_t ParseContext::Read(char* buf, size_t max_size) {
  const size_t copy_size = min<size_t>(stmt_len_ - stmt_offset_, max_size);
  if (copy_size > 0) {
    memcpy(buf, stmt_ + stmt_offset_, copy_size);
    stmt_offset_ += copy_size;
    return copy_size;
  }
  return 0;
}

}  // namespace sql.
}  // namespace yb.
