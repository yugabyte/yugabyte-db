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

import org.yb.tserver.TserverTypes.TabletServerErrorPB;

public class IsServerReadyResponse extends YRpcResponse {
  // Error status indicates the tserver is not ready.
  private TabletServerErrorPB serverError;
  // Number of tablets that are not in running state on this tserver.
  private int numNotRunning;
  // Total number of tablets that are managed under this tserver.
  private int totalTablets;

  public IsServerReadyResponse(long ellapsedMillis, String uuid,
      TabletServerErrorPB error, int notRunning, int totalTablets) {
    super(ellapsedMillis, uuid);
    this.serverError = error;
    this.numNotRunning = notRunning;
    this.totalTablets = totalTablets;
  }

  public boolean hasError() {
    return serverError != null;
  }

  public TabletServerErrorPB.Code getCode() {
    if (serverError == null) {
      return TabletServerErrorPB.Code.UNKNOWN_ERROR;
    }

    return serverError.getCode();
  }

  public String errorMessage() {
    if (serverError == null) {
      return "";
    }

    return serverError.getStatus().getMessage();
  }

  public int getNumNotRunningTablets() {
    return numNotRunning;
  }

  public int getTotalTablets() {
    return totalTablets;
  }
}
