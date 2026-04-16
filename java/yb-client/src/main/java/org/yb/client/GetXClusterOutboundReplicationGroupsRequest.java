// Copyright (c) YugabyteDB, Inc.

package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class GetXClusterOutboundReplicationGroupsRequest extends
    YRpc<GetXClusterOutboundReplicationGroupsResponse> {
  private final String namespaceId;

  GetXClusterOutboundReplicationGroupsRequest(YBTable table, String namespaceId) {
    super(table);
    this.namespaceId = namespaceId;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterReplicationOuterClass.GetXClusterOutboundReplicationGroupsRequestPB.Builder
      builder =
        MasterReplicationOuterClass.GetXClusterOutboundReplicationGroupsRequestPB.newBuilder();

    if (namespaceId != null) {
      builder.setNamespaceId(namespaceId);
    }

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "GetXClusterOutboundReplicationGroups";
  }

  @Override
  Pair<GetXClusterOutboundReplicationGroupsResponse, Object> deserialize(
      CallResponse callResponse, String tsUUID) throws Exception {
    MasterReplicationOuterClass.GetXClusterOutboundReplicationGroupsResponsePB.Builder builder =
      MasterReplicationOuterClass.GetXClusterOutboundReplicationGroupsResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);
    MasterTypes.MasterErrorPB error = builder.hasError() ? builder.getError() : null;

    GetXClusterOutboundReplicationGroupsResponse response =
        new GetXClusterOutboundReplicationGroupsResponse(
            deadlineTracker.getElapsedMillis(), tsUUID, error,
            builder.getReplicationGroupIdsList());

    return new Pair<>(response, error);
  }
}
