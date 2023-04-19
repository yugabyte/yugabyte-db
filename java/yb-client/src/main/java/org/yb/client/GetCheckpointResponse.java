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

public class GetCheckpointResponse extends YRpcResponse {

  private long index;
  private long term;
  private long snapshot_time;
  private byte[] snapshot_key;

  public GetCheckpointResponse(long elapsedMillis, String uuid, long index, long term,
                               long snapshot_time, byte[] snapshot_key) {
    super(elapsedMillis, uuid);
    this.index = index;
    this.term = term;
    this.snapshot_time = snapshot_time;
    this.snapshot_key = snapshot_key;
  }

  public long getIndex() {
    return index;
  }

  public long getTerm() {
    return term;
  }

  public long getSnapshotTime() {
    return snapshot_time;
  }

  public byte[] getSnapshotKey() {
    return snapshot_key;
  }
}
