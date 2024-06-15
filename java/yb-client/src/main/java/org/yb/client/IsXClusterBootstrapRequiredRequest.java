// Copyright (c) YugaByte, Inc.

package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class IsXClusterBootstrapRequiredRequest extends YRpc<IsXClusterBootstrapRequiredResponse>{

  private final String replicationGroupId;
  private final String namespaceId;

  public IsXClusterBootstrapRequiredRequest(
      YBTable table, String replicationGroupId, String namespaceId) {
    super(table);
    this.replicationGroupId = replicationGroupId;
    this.namespaceId = namespaceId;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterReplicationOuterClass.IsXClusterBootstrapRequiredRequestPB.Builder builder =
        MasterReplicationOuterClass.IsXClusterBootstrapRequiredRequestPB.newBuilder();
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
    return "IsXClusterBootstrapRequired";
  }

  @Override
  Pair<IsXClusterBootstrapRequiredResponse, Object> deserialize(
        CallResponse callResponse, String tsUUID) throws Exception {
    final MasterReplicationOuterClass.IsXClusterBootstrapRequiredResponsePB.Builder builder =
        MasterReplicationOuterClass.IsXClusterBootstrapRequiredResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);
    final MasterErrorPB error = builder.hasError() ? builder.getError() : null;
    final boolean notReady = builder.getNotReady();
    final boolean initialBootstrapRequired = builder.getInitialBootstrapRequired();;


        IsXClusterBootstrapRequiredResponse response =
        new IsXClusterBootstrapRequiredResponse(
          deadlineTracker.getElapsedMillis(),
          tsUUID,
          notReady,
          initialBootstrapRequired,
          error);
    return new Pair<>(response, error);
  }
}
