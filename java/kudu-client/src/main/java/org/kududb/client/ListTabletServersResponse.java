// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
package org.kududb.client;

import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;

import java.util.List;

@InterfaceAudience.Public
@InterfaceStability.Evolving
public class ListTabletServersResponse extends KuduRpcResponse {

  private final int tabletServersCount;
  private final List<String> tabletServersList;

  /**
   * @param ellapsedMillis Time in milliseconds since RPC creation to now.
   * @param tabletServersCount How many tablet servers the master is reporting.
   * @param tabletServersList List of tablet servers.
   */
  ListTabletServersResponse(long ellapsedMillis, String tsUUID,
                            int tabletServersCount, List<String> tabletServersList) {
    super(ellapsedMillis, tsUUID);
    this.tabletServersCount = tabletServersCount;
    this.tabletServersList = tabletServersList;
  }

  /**
   * Get the count of tablet servers as reported by the master.
   * @return TS count.
   */
  public int getTabletServersCount() {
    return tabletServersCount;
  }

  /**
   * Get the list of tablet servers, as represented by their hostname.
   * @return List of hostnames, one per TS.
   */
  public List<String> getTabletServersList() {
    return tabletServersList;
  }
}
