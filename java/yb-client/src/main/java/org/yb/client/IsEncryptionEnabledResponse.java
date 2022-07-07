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

import org.yb.master.MasterTypes.MasterErrorPB;

@InterfaceAudience.Public
public class IsEncryptionEnabledResponse extends YRpcResponse {
  private MasterErrorPB serverError;
  private boolean isEnabled;
  private String universeKeyId;

  public IsEncryptionEnabledResponse(
          long ellapsedMillis, String uuid, boolean isEnabled, String universeKeyId,
          MasterErrorPB serverError) {
    super(ellapsedMillis, uuid);
    this.serverError = serverError;
    this.isEnabled = isEnabled;
    this.universeKeyId = universeKeyId;
  }

  public MasterErrorPB getServerError() {
    return serverError;
  }

  public boolean getIsEnabled() {
    return isEnabled;
  }

  public String getUniverseKeyId() {
    return universeKeyId;
  }
}
