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
import org.yb.cdc.CdcService.GetChangesResponsePB;

@InterfaceAudience.Public
public class GetChangesResponse extends YRpcResponse {
  private final GetChangesResponsePB resp;

  GetChangesResponse(long ellapsedMillis, String uuid, GetChangesResponsePB resp) {
    super(ellapsedMillis, uuid);
    this.resp = resp;
  }

  public GetChangesResponsePB getResp() {
    return resp;
  }
}
