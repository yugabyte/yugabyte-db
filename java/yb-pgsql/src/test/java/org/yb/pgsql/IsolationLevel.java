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

package org.yb.pgsql;

import java.sql.Connection;

public enum IsolationLevel {
  // In stronger to weaker order:

  SERIALIZABLE(Connection.TRANSACTION_SERIALIZABLE, "SERIALIZABLE"),
  /** PG's/YB's REPEATABLE_READ is DocDB's SNAPSHOT under the hood. */
  REPEATABLE_READ(Connection.TRANSACTION_REPEATABLE_READ, "REPEATABLE READ"),
  READ_COMMITTED(Connection.TRANSACTION_READ_COMMITTED, "READ COMMITTED"),
  READ_UNCOMMITTED(Connection.TRANSACTION_READ_UNCOMMITTED, "READ UNCOMMITTED");

  public final int pgIsolationLevel;

  /** Name by which this isolation referenced in YSQL queries. */
  public final String sql;

  IsolationLevel(int pgIsolationLevel, String sql) {
    this.pgIsolationLevel = pgIsolationLevel;
    this.sql = sql;
  }
}
