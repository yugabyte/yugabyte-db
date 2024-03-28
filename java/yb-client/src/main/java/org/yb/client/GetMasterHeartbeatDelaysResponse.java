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
import org.yb.annotations.InterfaceStability;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.master.MasterAdminOuterClass.GetMasterHeartbeatDelaysResponsePB.MasterHeartbeatDelay;
import java.util.List;
import java.util.Map;

@InterfaceAudience.Public
@InterfaceStability.Evolving
public class GetMasterHeartbeatDelaysResponse extends YRpcResponse {

  private final MasterErrorPB serverError;
  private final Map<String, Long> masterHeartbeatDelays;

  GetMasterHeartbeatDelaysResponse(long elapsedMillis, String masterUUID,
                                          MasterErrorPB serverError,
                                          Map<String, Long> masterHeartbeatDelays) {
    super(elapsedMillis, masterUUID);
    this.serverError = serverError;
    this.masterHeartbeatDelays = masterHeartbeatDelays;
  }

  public MasterErrorPB getServerError() {
    return serverError;
  }

  public boolean hasError() {
    return serverError != null;
  }

  public String errorMessage() {
    return serverError != null ? serverError.getStatus().getMessage() : null;
  }

  public Map<String, Long> getMasterHeartbeatDelays() {
    return masterHeartbeatDelays;
  }
}
