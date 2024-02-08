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
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import io.netty.buffer.ByteBuf;
import javax.annotation.Nullable;
import org.yb.CommonNet;
import org.yb.CommonNet.HostPortPB;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;

public class SetupUniverseReplicationRequest extends YRpc<SetupUniverseReplicationResponse> {

  private final String replicationGroupName;
  private final Set<CommonNet.HostPortPB> sourceMasterAddresses;
  // A map of table ids to their bootstrap id if any.
  private final Map<String, String> sourceTableIdsBootstrapIdMap;
  private final Boolean transactional;

  SetupUniverseReplicationRequest(
    YBTable table,
    String replicationGroupName,
    Map<String, String> sourceTableIdsBootstrapIdMap,
    Set<HostPortPB> sourceMasterAddresses,
    @Nullable Boolean transactional) {
    super(table);
    this.replicationGroupName = replicationGroupName;
    this.sourceMasterAddresses = sourceMasterAddresses;
    this.sourceTableIdsBootstrapIdMap = sourceTableIdsBootstrapIdMap;
    this.transactional = transactional;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();

    // Add table IDs and bootstrap IDs.
    List<String> sourceTableIds = new ArrayList<>();
    List<String> sourceBootstrapIds = new ArrayList<>();
    sourceTableIdsBootstrapIdMap.forEach((tableId, bootstrapId) -> {
      sourceTableIds.add(tableId);
      sourceBootstrapIds.add(bootstrapId);
    });

    final MasterReplicationOuterClass.SetupUniverseReplicationRequestPB.Builder builder =
      MasterReplicationOuterClass.SetupUniverseReplicationRequestPB.newBuilder()
        .setReplicationGroupId(replicationGroupName)
        .addAllProducerTableIds(sourceTableIds)
        .addAllProducerMasterAddresses(sourceMasterAddresses);

    // If all bootstrap IDs are null, it is not required.
    if (sourceBootstrapIds.stream().anyMatch(Objects::nonNull)){
      builder.addAllProducerBootstrapIds(sourceBootstrapIds);
    }

    if(this.transactional != null) {
      builder.setTransactional(transactional);
    }

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
    final MasterReplicationOuterClass.SetupUniverseReplicationResponsePB.Builder builder =
      MasterReplicationOuterClass.SetupUniverseReplicationResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final MasterTypes.MasterErrorPB error = builder.hasError() ? builder.getError() : null;

    SetupUniverseReplicationResponse response =
      new SetupUniverseReplicationResponse(deadlineTracker.getElapsedMillis(), tsUUID, error);

    return new Pair<>(response, error);
  }
}
