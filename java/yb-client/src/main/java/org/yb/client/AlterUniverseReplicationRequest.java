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
import java.util.Set;
import java.util.UUID;
import org.jboss.netty.buffer.ChannelBuffer;
import org.yb.Common;
import org.yb.Common.HostPortPB;
import org.yb.master.Master;
import org.yb.util.Pair;

public class AlterUniverseReplicationRequest extends YRpc<AlterUniverseReplicationResponse> {

  private final UUID sourceUniverseUUID;
  private final Set<String> sourceTableIDsToAdd;
  private final Set<String> sourceTableIDsToRemove;
  private final Set<HostPortPB> sourceMasterAddresses;

  AlterUniverseReplicationRequest(
    YBTable table,
    UUID sourceUniverseUUID,
    Set<String> sourceTableIDsToAdd,
    Set<String> sourceTableIDsToRemove,
    Set<Common.HostPortPB> sourceMasterAddresses) {
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
  Pair<AlterUniverseReplicationResponse, Object> deserialize(
    CallResponse callResponse, String tsUUID) throws Exception {
    final Master.AlterUniverseReplicationResponsePB.Builder builder =
      Master.AlterUniverseReplicationResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final Master.MasterErrorPB error = builder.hasError() ? builder.getError() : null;

    AlterUniverseReplicationResponse response =
      new AlterUniverseReplicationResponse(deadlineTracker.getElapsedMillis(),
        tsUUID, error);

    return new Pair<>(response, error);
  }
}
