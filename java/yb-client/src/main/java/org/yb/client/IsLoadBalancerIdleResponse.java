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
import org.yb.master.MasterTypes;

@InterfaceAudience.Public
public class IsLoadBalancerIdleResponse extends YRpcResponse {
  private MasterTypes.MasterErrorPB serverError;

  public IsLoadBalancerIdleResponse(long ellapsedMillis,
                                    String masterUUID,
                                    MasterTypes.MasterErrorPB error) {
    super(ellapsedMillis, masterUUID);
    serverError = error;
  }

  public MasterTypes.MasterErrorPB getError() {
    return serverError;
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
