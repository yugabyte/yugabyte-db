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

import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterClusterOuterClass;
import org.yb.master.MasterTypes.MasterErrorPB;

@InterfaceAudience.Public
public class RollbackAutoFlagsResponse extends YRpcResponse {
  private int newConfigVersion;
  private boolean flagsRolledBack;
  private MasterErrorPB masterErrorPB;

  public RollbackAutoFlagsResponse(long elapsedMillis,
                                  String uuid,
                                  MasterClusterOuterClass.RollbackAutoFlagsResponsePB response) {
    super(elapsedMillis, uuid);
    this.newConfigVersion = response.getNewConfigVersion();
    this.flagsRolledBack = response.getFlagsRolledback();
    this.masterErrorPB = response.hasError() ? response.getError() : null;
  }

  public int getNewConfigVersion() {
    return this.newConfigVersion;
  }

  public ErrorCode getErrorCode() {
    return this.masterErrorPB.getStatus().getCode();
  }

  public boolean hasError() {
    return this.masterErrorPB != null;
  }

  public MasterErrorPB getError() {
    return this.masterErrorPB;
  }

  public boolean getFlagsRolledBack() {
    return this.flagsRolledBack;
  }
}
