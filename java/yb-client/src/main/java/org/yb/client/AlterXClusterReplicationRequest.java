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
import org.yb.Common;
import org.yb.master.Master;
import org.yb.util.Pair;

import java.util.List;
import java.util.UUID;

public class AlterXClusterReplicationRequest extends YRpc<AlterXClusterReplicationResponse> {

  private final UUID sourceUniverseUUID;
  private final List<String> sourceTableIDsToAdd;
  private final List<String> sourceTableIDsToRemove;
  private final List<Common.HostPortPB> sourceMasterAddresses;

  AlterXClusterReplicationRequest(
    YBTable table,
    UUID sourceUniverseUUID,
    List<String> sourceTableIDsToAdd,
    List<String> sourceTableIDsToRemove,
    List<Common.HostPortPB> sourceMasterAddresses) {
    super(table);
    this.sourceUniverseUUID = sourceUniverseUUID;
    this.sourceTableIDsToAdd = sourceTableIDsToAdd;
    this.sourceTableIDsToRemove = sourceTableIDsToRemove;
    this.sourceMasterAddresses = sourceMasterAddresses;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();

    final Master.AlterUniverseReplicationRequestPB.Builder builder =
      Master.AlterUniverseReplicationRequestPB.newBuilder()
        .setProducerId(sourceUniverseUUID.toString())
        .addAllProducerTableIdsToAdd(sourceTableIDsToAdd)
        .addAllProducerTableIdsToRemove(sourceTableIDsToRemove)
        .addAllProducerMasterAddresses(sourceMasterAddresses);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "AlterUniverseReplication";
  }

  @Override
  Pair<AlterXClusterReplicationResponse, Object> deserialize(
    CallResponse callResponse, String tsUUID) throws Exception {
    final Master.AlterUniverseReplicationResponsePB.Builder builder =
      Master.AlterUniverseReplicationResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final Master.MasterErrorPB error = builder.hasError() ? builder.getError() : null;

    AlterXClusterReplicationResponse response =
      new AlterXClusterReplicationResponse(deadlineTracker.getElapsedMillis(),
        tsUUID, error);

    return new Pair<>(response, error);
  }
}
