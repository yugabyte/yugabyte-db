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

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterClusterOuterClass;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class RollbackAutoFlagsRequest extends YRpc<RollbackAutoFlagsResponse> {
  private int rollbackVersion;

  public RollbackAutoFlagsRequest(YBTable table, int rollbackVersion) {
    // This table would be the master table from AsyncYBClient since this service is registered
    // on master.
    super(table);
    this.rollbackVersion = rollbackVersion;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert  header.isInitialized();
    final MasterClusterOuterClass.RollbackAutoFlagsRequestPB.Builder builder =
      MasterClusterOuterClass.RollbackAutoFlagsRequestPB.newBuilder();
    builder.setRollbackVersion(this.rollbackVersion);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "RollbackAutoFlags";
  }

  @Override
  Pair<RollbackAutoFlagsResponse, Object> deserialize(
    CallResponse callResponse, String uuid) throws Exception {
    final MasterClusterOuterClass.RollbackAutoFlagsResponsePB.Builder respBuilder =
      MasterClusterOuterClass.RollbackAutoFlagsResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    RollbackAutoFlagsResponse response = new RollbackAutoFlagsResponse(
        deadlineTracker.getElapsedMillis(), uuid, respBuilder.build());
    return new Pair<RollbackAutoFlagsResponse, Object>(
        response, null);
  }
}
