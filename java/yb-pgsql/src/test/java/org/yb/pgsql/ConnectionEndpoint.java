// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.pgsql;

// Descibes the port on which connection is to be made.
public enum ConnectionEndpoint {
  // There are two ports on which connections can be made to
  // - Ysql Connection Manager port.
  // - Postgres port.
  YSQL_CONN_MGR(true), // Ysql Connection Manager
  POSTGRES(false); // Postgres

  ConnectionEndpoint(boolean enabled) {
    this.enabled = enabled;
  }

  public final boolean enabled;
  public static ConnectionEndpoint DEFAULT = POSTGRES;
}
