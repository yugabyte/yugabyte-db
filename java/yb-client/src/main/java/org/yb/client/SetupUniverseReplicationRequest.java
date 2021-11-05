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
import org.jboss.netty.buffer.ChannelBuffer;
import org.yb.Common;
import org.yb.Common.HostPortPB;
import org.yb.master.Master;
import org.yb.util.Pair;

public class SetupUniverseReplicationRequest extends YRpc<SetupUniverseReplicationResponse> {

  private final String replicationGroupName;
  private final Set<String> sourceTableIDs;
  private final Set<Common.HostPortPB> sourceMasterAddresses;

  SetupUniverseReplicationRequest(
    YBTable table,
    String replicationGroupName,
    Set<String> sourceTableIDs,
    Set<HostPortPB> sourceMasterAddresses) {
    super(table);
    this.replicationGroupName = replicationGroupName;
    this.sourceTableIDs = sourceTableIDs;
    this.sourceMasterAddresses = sourceMasterAddresses;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();

    final Master.SetupUniverseReplicationRequestPB.Builder builder =
      Master.SetupUniverseReplicationRequestPB.newBuilder()
        .setProducerId(replicationGroupName)
        .addAllProducerTableIds(sourceTableIDs)
        .addAllProducerMasterAddresses(sourceMasterAddresses);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "SetupUniverseReplication";
  }

  @Override
  Pair<SetupUniverseReplicationResponse, Object> deserialize(
    CallResponse callResponse, String tsUUID) throws Exception {
    final Master.SetupUniverseReplicationResponsePB.Builder builder =
      Master.SetupUniverseReplicationResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final Master.MasterErrorPB error = builder.hasError() ? builder.getError() : null;

    SetupUniverseReplicationResponse response =
      new SetupUniverseReplicationResponse(deadlineTracker.getElapsedMillis(), tsUUID, error);

    return new Pair<>(response, error);
  }
}
