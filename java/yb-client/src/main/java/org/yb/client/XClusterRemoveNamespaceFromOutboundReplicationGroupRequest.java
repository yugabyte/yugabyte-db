// Copyright (c) YugaByte, Inc.

package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class XClusterRemoveNamespaceFromOutboundReplicationGroupRequest
    extends YRpc<XClusterRemoveNamespaceFromOutboundReplicationGroupResponse> {

  private final String replicationGroupId;
  private final String namespaceId;

  public XClusterRemoveNamespaceFromOutboundReplicationGroupRequest(
      YBTable table, String replicationGroupId, String namespaceId) {
    super(table);
    this.replicationGroupId = replicationGroupId;
    this.namespaceId = namespaceId;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterReplicationOuterClass.XClusterRemoveNamespaceFromOutboundReplicationGroupRequestPB
            .Builder
        builder =
            MasterReplicationOuterClass.XClusterRemoveNamespaceFromOutboundReplicationGroupRequestPB
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
    return "XClusterRemoveNamespaceFromOutboundReplicationGroup";
  }

  @Override
  Pair<XClusterRemoveNamespaceFromOutboundReplicationGroupResponse, Object> deserialize(
      CallResponse callResponse, String tsUUID) throws Exception {
    final MasterReplicationOuterClass.XClusterRemoveNamespaceFromOutboundReplicationGroupResponsePB
            .Builder
        builder =
            MasterReplicationOuterClass.
            XClusterRemoveNamespaceFromOutboundReplicationGroupResponsePB
                .newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);
    final MasterErrorPB error = builder.hasError() ? builder.getError() : null;

    XClusterRemoveNamespaceFromOutboundReplicationGroupResponse response =
        new XClusterRemoveNamespaceFromOutboundReplicationGroupResponse(
            deadlineTracker.getElapsedMillis(), tsUUID, error);
    return new Pair<>(response, error);
  }
}
