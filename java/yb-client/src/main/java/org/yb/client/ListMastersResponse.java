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

package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.util.ServerInfo;

import java.util.List;

@InterfaceAudience.Public
public class ListMastersResponse extends YRpcResponse {
  private boolean hasError;
  private final List<ServerInfo> mastersList;

  ListMastersResponse(
      long ellapsedMillis, String masterUUID, boolean hasErr, List<ServerInfo> mastersList) {
    super(ellapsedMillis, masterUUID);
    hasError = hasErr;
    this.mastersList = mastersList;
  }

  public boolean hasError() {
    return hasError;
  }

  public int getNumMasters() {
    return mastersList.size();
  }

  public List<ServerInfo> getMasters() {
    return mastersList;
  }
}
