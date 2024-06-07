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
import org.yb.cdc.CdcService;
import org.yb.cdc.CdcService.GetChangesResponsePB;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class GetChangesResponse extends YRpcResponse {
  private final GetChangesResponsePB resp;

  private byte[] key;
  private int writeId;

  GetChangesResponse(long ellapsedMillis, String uuid,
                     GetChangesResponsePB resp, byte[] key, int writeId) {
    super(ellapsedMillis, uuid);
    this.key = key;
    this.writeId = writeId;
    this.resp = resp;
  }

  public GetChangesResponsePB getResp() {
    return resp;
  }

  public byte[] getKey() {
    return key;
  }

  public int getWriteId() {
    return writeId;
  }

  public long getTerm() {
    return getResp().getCdcSdkCheckpoint().getTerm();
  }

  public long getIndex() {
    return getResp().getCdcSdkCheckpoint().getIndex();
  }

  public long getSnapshotTime() {
    return getResp().getCdcSdkCheckpoint().getSnapshotTime();
  }

  public int getWalSegmentIndex() {
    return getResp().getWalSegmentIndex();
  }
}
