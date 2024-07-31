// Copyright (c) YugaByte, Inc.

package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class GetUniverseReplicationInfoRequest extends YRpc<GetUniverseReplicationInfoResponse> {
  private final String replicationGroupName;

  GetUniverseReplicationInfoRequest(YBTable table, String replicationGroupName) {
    super(table);
    this.replicationGroupName = replicationGroupName;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    MasterReplicationOuterClass.GetUniverseReplicationInfoRequestPB.Builder builder =
      MasterReplicationOuterClass.GetUniverseReplicationInfoRequestPB.newBuilder();
    if (replicationGroupName != null) {
      builder.setReplicationGroupId(replicationGroupName);
    }
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "GetUniverseReplicationInfo";
  }

  @Override
  Pair<GetUniverseReplicationInfoResponse, Object> deserialize(
    CallResponse callResponse,
    String tsUUID) throws Exception {
    MasterReplicationOuterClass.GetUniverseReplicationInfoResponsePB.Builder builder =
      MasterReplicationOuterClass.GetUniverseReplicationInfoResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);
    MasterTypes.MasterErrorPB error = builder.hasError() ? builder.getError() : null;

    GetUniverseReplicationInfoResponse response =
      new GetUniverseReplicationInfoResponse(deadlineTracker.getElapsedMillis(), tsUUID, error,
          builder.getReplicationType(), builder.getDbScopedInfosList(), builder.getTableInfosList());

    return new Pair<>(response, error);
  }
}
