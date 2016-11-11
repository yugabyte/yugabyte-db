//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// SessionContext represents the environment where the parser is running.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/session_context.h"

namespace yb {
namespace sql {

using std::shared_ptr;

using client::YBClient;
using client::YBSession;
using client::YBTable;

SessionContext::SessionContext(int session_id,
                               std::shared_ptr<YBClient> client,
                               std::shared_ptr<YBSession> session)
    : session_id_(session_id),
      client_(client),
      session_(session) {
}

} // namespace sql
} // namespace yb
