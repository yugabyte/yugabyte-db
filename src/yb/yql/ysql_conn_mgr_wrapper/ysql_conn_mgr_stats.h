// Copyright (c) YugabyteDB, Inc.
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

#pragma once

#define YSQL_CONN_MGR_SHMEM_KEY_ENV_NAME "YSQL_CONN_MGR_STATS_KEY"
#define YSQL_CONN_MGR_WARMUP_DB "YB_YSQL_CONN_MGR_WARMUP_DB"
#define YSQL_CONN_MGR_MAX_POOLS 100
#define DB_NAME_MAX_LEN 64
#define USER_NAME_MAX_LEN DB_NAME_MAX_LEN

#define POOL_PER_USER_DB 0
#define POOL_PER_DB 1

#define YB_YSQL_CONN_MGR_POOL_MODE POOL_PER_USER_DB
#define YB_POOL_MODE YB_YSQL_CONN_MGR_POOL_MODE


struct ConnectionStats {
  uint64_t active_clients;
  uint64_t queued_clients;
  uint64_t waiting_clients;
  uint64_t active_servers;
  uint64_t idle_servers;
  uint64_t query_rate;
  uint64_t transaction_rate;
  uint64_t avg_wait_time_ns;
  uint64_t last_updated_timestamp;
  char database_name[DB_NAME_MAX_LEN];
  char user_name[USER_NAME_MAX_LEN];
};
