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
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;

public class GetUniverseReplicationRequest extends YRpc<GetUniverseReplicationResponse> {

  private final String replicationGroupName;

  GetUniverseReplicationRequest(YBTable table, String replicationGroupName) {
    super(table);
    this.replicationGroupName = replicationGroupName;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();

    final MasterReplicationOuterClass.GetUniverseReplicationRequestPB.Builder builder =
        MasterReplicationOuterClass.GetUniverseReplicationRequestPB.newBuilder()
            .setReplicationGroupId(replicationGroupName);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "GetUniverseReplication";
  }

  @Override
  Pair<GetUniverseReplicationResponse, Object> deserialize(
    CallResponse callResponse, String tsUUID) throws Exception {
    final MasterReplicationOuterClass.GetUniverseReplicationResponsePB.Builder builder =
      MasterReplicationOuterClass.GetUniverseReplicationResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final MasterTypes.MasterErrorPB error = builder.hasError() ? builder.getError() : null;
    final CatalogEntityInfo.SysUniverseReplicationEntryPB info = builder.getEntry();

    GetUniverseReplicationResponse response =
      new GetUniverseReplicationResponse(deadlineTracker.getElapsedMillis(), tsUUID, error, info);

    return new Pair<>(response, error);
  }
}
