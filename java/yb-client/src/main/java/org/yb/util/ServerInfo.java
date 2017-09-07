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

package org.yb.util;

// Class to track common info provided by master or tablet server.
public class ServerInfo {
  private String uuid;
  private String host;
  private int port;
  // Note: Need not be set when there is no leader (eg., when all tablet servers are listed).
  private boolean isLeader;

  public ServerInfo(String uuid, String host, int port, boolean isLeader) {
    this.uuid = uuid;
    this.host = host;
    this.port = port;
    this.isLeader = isLeader;
  }

  public String getHost() {
    return host;
  }

  public int getPort() {
    return port;
  }

  public String getUuid() {
    return uuid;
  }

  public boolean isLeader() {
    return isLeader;
  }
}
