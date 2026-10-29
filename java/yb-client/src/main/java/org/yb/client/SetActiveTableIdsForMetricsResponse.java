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

package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.tserver.TserverTypes.TabletServerErrorPB;

@InterfaceAudience.Public
public class SetActiveTableIdsForMetricsResponse extends YRpcResponse {

  private final TabletServerErrorPB error;
  private final boolean filteringEnabled;

  public SetActiveTableIdsForMetricsResponse(
      long elapsedMillis, String uuid, TabletServerErrorPB error, boolean filteringEnabled) {
    super(elapsedMillis, uuid);
    this.error = error;
    this.filteringEnabled = filteringEnabled;
  }

  /** Whether the TServer rejected the list. The previously accepted list stays active then. */
  public boolean hasError() {
    return error != null;
  }

  public TabletServerErrorPB getError() {
    return error;
  }

  /**
   * State of the TServer's enable_active_table_metrics_filtering kill switch. The list is stored
   * either way, but no scrape uses it while this is false.
   */
  public boolean isFilteringEnabled() {
    return filteringEnabled;
  }
}
