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
import org.jboss.netty.buffer.ChannelBuffer;
import org.yb.master.Master;
import org.yb.util.Pair;

import java.util.UUID;

public class SetXClusterReplicationActiveRequest
  extends YRpc<SetXClusterReplicationActiveResponse> {

  private final UUID sourceUniverseUUID;
  private final boolean active;

  SetXClusterReplicationActiveRequest(YBTable table, UUID sourceUniverseUUID, boolean active) {
    super(table);
    this.sourceUniverseUUID = sourceUniverseUUID;
    this.active = active;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();

    final Master.SetUniverseReplicationEnabledRequestPB.Builder builder =
      Master.SetUniverseReplicationEnabledRequestPB.newBuilder()
        .setProducerId(sourceUniverseUUID.toString())
        .setIsEnabled(active);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "SetUniverseReplicationEnabled";
  }

  @Override
  Pair<SetXClusterReplicationActiveResponse, Object> deserialize(
    CallResponse callResponse, String tsUUID) throws Exception {
    final Master.SetUniverseReplicationEnabledResponsePB.Builder builder =
      Master.SetUniverseReplicationEnabledResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final Master.MasterErrorPB error = builder.hasError() ? builder.getError() : null;

    SetXClusterReplicationActiveResponse response =
      new SetXClusterReplicationActiveResponse(deadlineTracker.getElapsedMillis(),
        tsUUID, error);

    return new Pair<>(response, error);
  }
}
