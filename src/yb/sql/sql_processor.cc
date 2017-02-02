//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/sql_processor.h"

namespace yb {
namespace sql {

using std::shared_ptr;
using std::string;
using client::YBClient;
using client::YBSession;

SqlProcessor::SqlProcessor(shared_ptr<YBClient> client)
    : ybsql_(new YbSql()),
      client_(client),
      write_session_(client->NewSession(false)),
      read_session_(client->NewSession(true)),
      sql_env_(new SqlEnv(client_, write_session_, read_session_)),
      is_used_(false) {

  write_session_->SetTimeoutMillis(kSessionTimeoutMs);
  CHECK_OK(write_session_->SetFlushMode(YBSession::MANUAL_FLUSH));

  read_session_->SetTimeoutMillis(kSessionTimeoutMs);
  CHECK_OK(read_session_->SetFlushMode(YBSession::MANUAL_FLUSH));
}

SqlProcessor::~SqlProcessor() {
}

CHECKED_STATUS SqlProcessor::Run(const string& sql_stmt) {
  sql_env_->Reset();
  return ybsql_->Process(sql_env_.get(), sql_stmt);
}

}  // namespace sql
}  // namespace yb
