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

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterClusterOuterClass.GetLoadBalancerStateResponsePB;
import org.yb.master.MasterTypes.MasterErrorPB;

@InterfaceAudience.Public
public class GetLoadBalancerStateResponse extends YRpcResponse {

  public static final Logger LOG = LoggerFactory.getLogger(GetLoadBalancerStateResponse.class);

  private GetLoadBalancerStateResponsePB masterLBState;

  public GetLoadBalancerStateResponse(
      long elapsedMillis, String uuid, GetLoadBalancerStateResponsePB response) {
    super(elapsedMillis, uuid);
    this.masterLBState = response;
  }

  public MasterErrorPB getServerError() {
    return masterLBState.getError();
  }

  public boolean hasError() {
    return masterLBState.hasError();
  }

  public String errorMessage() {
    return masterLBState.hasError() ? masterLBState.getError().getStatus().getMessage() : null;
  }

  public boolean hasIsEnabled() {
    return masterLBState.hasIsEnabled();
  }

  public boolean isEnabled() {
    return masterLBState.getIsEnabled();
  }
}
