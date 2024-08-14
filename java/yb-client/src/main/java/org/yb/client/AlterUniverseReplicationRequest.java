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
import org.yb.CommonNet;
import org.yb.CommonNet.HostPortPB;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;

import java.util.*;

public class AlterUniverseReplicationRequest extends YRpc<AlterUniverseReplicationResponse> {

  private final String replicationGroupName;
  private final Map<String, String> sourceTableIdsToAddBootstrapIdMap;
  private final Set<String> sourceTableIdsToRemove;
  private final Set<HostPortPB> sourceMasterAddresses;
  private final String newReplicationGroupName;
  private final boolean removeTableIgnoreErrors;
  // Must be null for table level remove to be used.
  private final String producerNamespaceIdToRemove;

  AlterUniverseReplicationRequest(
    YBTable table,
    String replicationGroupName,
    String producerNamespaceIdToRemove) {
    super(table);
    this.replicationGroupName = replicationGroupName;
    this.sourceTableIdsToAddBootstrapIdMap = new HashMap<>();
    this.sourceTableIdsToRemove = new HashSet<>();
    this.sourceMasterAddresses = new HashSet<>();
    this.newReplicationGroupName = null;
    this.removeTableIgnoreErrors = true;
    this.producerNamespaceIdToRemove = producerNamespaceIdToRemove;
  }


  AlterUniverseReplicationRequest(
    YBTable table,
    String replicationGroupName,
    Map<String, String> sourceTableIdsToAddBootstrapIdMap,
    Set<String> sourceTableIdsToRemove,
    Set<CommonNet.HostPortPB> sourceMasterAddresses,
    String newReplicationGroupName,
    boolean removeTableIgnoreErrors) {
    super(table);
    this.replicationGroupName = replicationGroupName;
    this.sourceTableIdsToAddBootstrapIdMap = sourceTableIdsToAddBootstrapIdMap;
    this.sourceTableIdsToRemove = sourceTableIdsToRemove;
    this.sourceMasterAddresses = sourceMasterAddresses;
    this.newReplicationGroupName = newReplicationGroupName;
    this.removeTableIgnoreErrors = removeTableIgnoreErrors;
    this.producerNamespaceIdToRemove = null;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();

    // Add table IDs and bootstrap IDs.
    List<String> sourceTableIdsToAdd = new ArrayList<>();
    List<String> sourceBootstrapIdstoAdd = new ArrayList<>();
    sourceTableIdsToAddBootstrapIdMap.forEach((tableId, bootstrapId) -> {
      sourceTableIdsToAdd.add(tableId);
      sourceBootstrapIdstoAdd.add(bootstrapId);
    });

    final MasterReplicationOuterClass.AlterUniverseReplicationRequestPB.Builder builder =
        MasterReplicationOuterClass.AlterUniverseReplicationRequestPB.newBuilder()
            .setReplicationGroupId(replicationGroupName)
            .addAllProducerMasterAddresses(sourceMasterAddresses)
            .addAllProducerTableIdsToAdd(sourceTableIdsToAdd)
            .addAllProducerTableIdsToRemove(sourceTableIdsToRemove)
            .setRemoveTableIgnoreErrors(removeTableIgnoreErrors);

    if (newReplicationGroupName != null) {
      builder.setNewReplicationGroupId(newReplicationGroupName);
    }

    // If all bootstrap IDs are null, it is not required.
    if (sourceBootstrapIdstoAdd.stream().anyMatch(Objects::nonNull)){
      builder.addAllProducerBootstrapIdsToAdd(sourceBootstrapIdstoAdd);
    }

    if (producerNamespaceIdToRemove != null) {
      builder.setProducerNamespaceIdToRemove(producerNamespaceIdToRemove);
    }

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
    final MasterReplicationOuterClass.AlterUniverseReplicationResponsePB.Builder builder =
      MasterReplicationOuterClass.AlterUniverseReplicationResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final MasterTypes.MasterErrorPB error = builder.hasError() ? builder.getError() : null;

    AlterUniverseReplicationResponse response =
      new AlterUniverseReplicationResponse(deadlineTracker.getElapsedMillis(),
        tsUUID, error);

    return new Pair<>(response, error);
  }
}
