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

import java.util.List;

import org.yb.cdc.CdcService.TabletCheckpointPair;

public class GetTabletListToPollForCDCResponse extends YRpcResponse {
  private List<TabletCheckpointPair> tabletCheckpointPairList;

  public GetTabletListToPollForCDCResponse(
      long elapsedMillis,
      String uuid,
      List<TabletCheckpointPair> tabletCheckpointPairList) {
    super(elapsedMillis, uuid);
    this.tabletCheckpointPairList = tabletCheckpointPairList;
  }

  /**
   * Get the list of tablet checkpoint pairs as specified in the request
   * @return a list of tablet checkpoint pairs
   */
  public List<TabletCheckpointPair> getTabletCheckpointPairList() {
    return this.tabletCheckpointPairList;
  }

  /**
   * Get the count of tablet checkpoint pairs in the list
   * @return a count of items in the tablet checkpoint pair list
   */
  public int getTabletCheckpointPairListSize() {
    return this.tabletCheckpointPairList.size();
  }
}
