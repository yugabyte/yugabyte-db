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
import java.util.List;
import org.jboss.netty.buffer.ChannelBuffer;
import org.yb.WireProtocol;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;

public class DeleteUniverseReplicationRequest extends YRpc<DeleteUniverseReplicationResponse> {

  private final String replicationGroupName;
  // Default value is false.
  private final boolean ignoreErrors;

  DeleteUniverseReplicationRequest(
      YBTable table, String replicationGroupName, boolean ignoreErrors) {
    super(table);
    this.replicationGroupName = replicationGroupName;
    this.ignoreErrors = ignoreErrors;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();

    final MasterReplicationOuterClass.DeleteUniverseReplicationRequestPB.Builder builder =
        MasterReplicationOuterClass.DeleteUniverseReplicationRequestPB.newBuilder()
            .setProducerId(replicationGroupName)
            .setIgnoreErrors(ignoreErrors);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "DeleteUniverseReplication";
  }

  @Override
  Pair<DeleteUniverseReplicationResponse, Object> deserialize(
    CallResponse callResponse, String tsUUID) throws Exception {
    final MasterReplicationOuterClass.DeleteUniverseReplicationResponsePB.Builder builder =
      MasterReplicationOuterClass.DeleteUniverseReplicationResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final MasterTypes.MasterErrorPB error = builder.hasError() ? builder.getError() : null;
    final List<WireProtocol.AppStatusPB> warnings = builder.getWarningsList();

    DeleteUniverseReplicationResponse response =
      new DeleteUniverseReplicationResponse(deadlineTracker.getElapsedMillis(),
        tsUUID, error, warnings);

    return new Pair<>(response, error);
  }
}
