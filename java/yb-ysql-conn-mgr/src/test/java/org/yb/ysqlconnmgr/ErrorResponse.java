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

package org.yb.ysqlconnmgr;

import java.util.Map;

public class ErrorResponse {
  // Prefix that YbThrowError handling in postgres.c puts on a ConnMgr-raised message.
  public static final String CONN_MGR_PREFIX = "ConnMgr originated error: ";

  private final Map<Character, String> fields;

  private ErrorResponse(Map<Character, String> fields) {
    this.fields = fields;
  }

  public static ErrorResponse parse(byte[] body) {
    return new ErrorResponse(PgWireProtocol.parseErrorResponse(body));
  }

  public String severity() {
    return fields.get('S');
  }

  public String sqlstate() {
    return fields.get('C');
  }

  public String message() {
    return fields.get('M');
  }

  @Override
  public String toString() {
    return severity() + " " + sqlstate() + " " + message();
  }
}
