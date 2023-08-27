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

import java.nio.ByteOrder;
import java.util.UUID;

// Class to track common info provided by master or tablet server.
public class ServerInfo {
  private String uuid;
  private String host;
  private int port;
  // Note: Need not be set when there is no leader (eg., when all tablet servers are listed).
  private boolean isLeader;
  private String state;

  public ServerInfo(String uuid, String host, int port, boolean isLeader, String state) {
    this.uuid = uuid;
    this.host = host;
    this.port = port;
    this.isLeader = isLeader;
    this.state = state;
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

  public String getState() {
    return state;
  }

  // Converts a UUID to string in host byte-order, which is how UUIDs are shown in web server and
  // log.
  public static String UUIDToHostString(UUID uuid) {
    // Strip "-".
    String id = uuid.toString().replaceAll("\\-", "");
    if (ByteOrder.nativeOrder().equals(ByteOrder.BIG_ENDIAN)) {
      return id;
    }
    // Reverse byte-order if host is not in network (big-endian) order.
    StringBuilder sb = new StringBuilder(id.length());
    for (int pos = id.length() - 2; pos >= 0; pos -= 2) {
      sb.append(id.substring(pos, pos + 2));
    }
    return sb.toString();
  }

  @Override
  public String toString() {
    return "ServerInfo "
        + "for server="
        + uuid
        + " host="
        + host
        + ", port="
        + port
        + ", isLeader="
        + isLeader
        + ", state="
        + state;
  }
}
