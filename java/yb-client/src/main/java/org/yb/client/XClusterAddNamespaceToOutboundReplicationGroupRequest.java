// Copyright (c) YugaByte, Inc.

package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class XClusterAddNamespaceToOutboundReplicationGroupRequest
    extends YRpc<XClusterAddNamespaceToOutboundReplicationGroupResponse> {

  private final String replicationGroupId;
  private final String namespaceId;

  public XClusterAddNamespaceToOutboundReplicationGroupRequest(
      YBTable table, String replicationGroupId, String namespaceId) {
    super(table);
    this.replicationGroupId = replicationGroupId;
    this.namespaceId = namespaceId;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterReplicationOuterClass.XClusterAddNamespaceToOutboundReplicationGroupRequestPB
            .Builder
        builder =
            MasterReplicationOuterClass.XClusterAddNamespaceToOutboundReplicationGroupRequestPB
                .newBuilder();
    builder.setReplicationGroupId(this.replicationGroupId);
    builder.setNamespaceId(this.namespaceId);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "XClusterAddNamespaceToOutboundReplicationGroup";
  }

  @Override
  Pair<XClusterAddNamespaceToOutboundReplicationGroupResponse, Object> deserialize(
      CallResponse callResponse, String tsUUID) throws Exception {
    final MasterReplicationOuterClass.XClusterAddNamespaceToOutboundReplicationGroupResponsePB
            .Builder
        builder =
            MasterReplicationOuterClass.XClusterAddNamespaceToOutboundReplicationGroupResponsePB
                .newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);
    final MasterErrorPB error = builder.hasError() ? builder.getError() : null;

    XClusterAddNamespaceToOutboundReplicationGroupResponse response =
        new XClusterAddNamespaceToOutboundReplicationGroupResponse(
            deadlineTracker.getElapsedMillis(), tsUUID, error);
    return new Pair<>(response, error);
  }
}
