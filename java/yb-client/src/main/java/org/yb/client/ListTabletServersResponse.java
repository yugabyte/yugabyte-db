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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.annotations.InterfaceStability;
import org.yb.master.MasterTypes;
import org.yb.util.ServerInfo;

import java.util.List;

@InterfaceAudience.Public
@InterfaceStability.Evolving
public class ListTabletServersResponse extends YRpcResponse {

  private final int tabletServersCount;
  private final List<ServerInfo> tabletServersList;
  private MasterTypes.MasterErrorPB serverError;

  /**
   * @param ellapsedMillis Time in milliseconds since RPC creation to now.
   * @param tabletServersCount How many tablet servers the master is reporting.
   * @param tabletServersList List of tablet servers.
   */
  ListTabletServersResponse(long ellapsedMillis, String tsUUID, int tabletServersCount,
                            List<ServerInfo> tabletServersList, MasterTypes.MasterErrorPB error) {
    super(ellapsedMillis, tsUUID);
    this.tabletServersCount = tabletServersCount;
    this.tabletServersList = tabletServersList;
    serverError = error;
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
  public List<ServerInfo> getTabletServersList() {
    return tabletServersList;
  }

  public boolean hasError() {
    return serverError != null;
  }

  public String errorMessage() {
    if (serverError == null) {
      return "";
    }

    return serverError.getStatus().getMessage();
  }
}
