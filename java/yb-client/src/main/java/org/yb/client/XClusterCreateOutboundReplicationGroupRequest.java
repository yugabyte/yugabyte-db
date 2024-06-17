// Copyright (c) YugaByte, Inc.

package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import java.util.Set;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.util.Pair;


@InterfaceAudience.Public
public class XClusterCreateOutboundReplicationGroupRequest
    extends YRpc<XClusterCreateOutboundReplicationGroupResponse> {

  private final String replicationGroupId;
  private final Set<String> namespaceIds;

  XClusterCreateOutboundReplicationGroupRequest(
      YBTable table, String replicationGroupId, Set<String> namespaceIds) {
    super(table);
    this.replicationGroupId = replicationGroupId;
    this.namespaceIds = namespaceIds;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterReplicationOuterClass.XClusterCreateOutboundReplicationGroupRequestPB.Builder
      builder =
        MasterReplicationOuterClass.XClusterCreateOutboundReplicationGroupRequestPB.newBuilder();
    builder.setReplicationGroupId(this.replicationGroupId);
    builder.addAllNamespaceIds(this.namespaceIds);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "XClusterCreateOutboundReplicationGroup";
  }

  @Override
  Pair<XClusterCreateOutboundReplicationGroupResponse, Object> deserialize(
      CallResponse callResponse, String tsUUID) throws Exception {
    final MasterReplicationOuterClass.XClusterCreateOutboundReplicationGroupResponsePB.Builder
      builder =
        MasterReplicationOuterClass.XClusterCreateOutboundReplicationGroupResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);
    final MasterErrorPB error = builder.hasError() ? builder.getError() : null;

    XClusterCreateOutboundReplicationGroupResponse response =
        new XClusterCreateOutboundReplicationGroupResponse(
          deadlineTracker.getElapsedMillis(),
          tsUUID,
          error);
    return new Pair<>(response, error);
  }
}
