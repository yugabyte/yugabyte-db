// Copyright (c) YugaByte, Inc.

package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import java.util.Set;
import org.yb.CommonNet.HostPortPB;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class AddNamespaceToXClusterReplicationRequest
    extends YRpc<AddNamespaceToXClusterReplicationResponse> {
  private final String replicationGroupId;

  private final Set<HostPortPB> targetMasterAddresses;

  private final String namespaceId;

  public AddNamespaceToXClusterReplicationRequest(
      YBTable table,
      String replicationGroupId,
      Set<HostPortPB> targetMasterAddresses,
      String namespaceId) {
    super(table);
    this.replicationGroupId = replicationGroupId;
    this.targetMasterAddresses = targetMasterAddresses;
    this.namespaceId = namespaceId;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterReplicationOuterClass.AddNamespaceToXClusterReplicationRequestPB.Builder builder =
        MasterReplicationOuterClass.AddNamespaceToXClusterReplicationRequestPB.newBuilder();
    builder.setReplicationGroupId(this.replicationGroupId);
    builder.addAllTargetMasterAddresses(this.targetMasterAddresses);
    builder.setNamespaceId(this.namespaceId);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "AddNamespaceToXClusterReplication";
  }

  @Override
  Pair<AddNamespaceToXClusterReplicationResponse, Object> deserialize(
      CallResponse callResponse, String tsUUID) throws Exception {
    final MasterReplicationOuterClass.AddNamespaceToXClusterReplicationResponsePB.Builder builder =
        MasterReplicationOuterClass.AddNamespaceToXClusterReplicationResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);
    final MasterErrorPB error = builder.hasError() ? builder.getError() : null;

    AddNamespaceToXClusterReplicationResponse response =
        new AddNamespaceToXClusterReplicationResponse(
            deadlineTracker.getElapsedMillis(), tsUUID, error);
    return new Pair<>(response, error);
  }
}
