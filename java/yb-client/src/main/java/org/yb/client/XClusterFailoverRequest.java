// Copyright (c) YugabyteDB, Inc.
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
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;

public class XClusterFailoverRequest extends YRpc<XClusterFailoverResponse> {

  private final String replicationGroupId;

  XClusterFailoverRequest(YBTable table, String replicationGroupId) {
    super(table);
    this.replicationGroupId = replicationGroupId;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterReplicationOuterClass.XClusterFailoverRequestPB.Builder builder =
        MasterReplicationOuterClass.XClusterFailoverRequestPB.newBuilder()
            .setReplicationGroupId(replicationGroupId);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "XClusterFailover";
  }

  @Override
  Pair<XClusterFailoverResponse, Object> deserialize(
      CallResponse callResponse, String tsUUID) throws Exception {
    final MasterReplicationOuterClass.XClusterFailoverResponsePB.Builder builder =
        MasterReplicationOuterClass.XClusterFailoverResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), builder);
    final MasterTypes.MasterErrorPB error = builder.hasError() ? builder.getError() : null;
    XClusterFailoverResponse response =
        new XClusterFailoverResponse(deadlineTracker.getElapsedMillis(), tsUUID, error);
    return new Pair<>(response, error);
  }
}
