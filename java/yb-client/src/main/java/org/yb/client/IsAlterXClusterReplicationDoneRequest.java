// Copyright (c) YugaByte, Inc.

package org.yb.client;

import org.yb.WireProtocol.AppStatusPB;
import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import java.util.Set;
import org.yb.CommonNet.HostPortPB;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class IsAlterXClusterReplicationDoneRequest extends
    YRpc<IsAlterXClusterReplicationDoneResponse> {
  private final String replicationGroupId;

  // If target addresses are set, will delete replication on both source and target.
  // If target addresses is null, will only delete outbound replication from the source.
  private final Set<HostPortPB> targetMasterAddresses;

  public IsAlterXClusterReplicationDoneRequest(
      YBTable table, String replicationGroupId, Set<HostPortPB> targetMasterAddresses) {
    super(table);
    this.replicationGroupId = replicationGroupId;
    this.targetMasterAddresses = targetMasterAddresses;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterReplicationOuterClass.IsAlterXClusterReplicationDoneRequestPB.Builder
      builder =
        MasterReplicationOuterClass.IsAlterXClusterReplicationDoneRequestPB.newBuilder();
    builder.setReplicationGroupId(this.replicationGroupId);
    if (targetMasterAddresses != null) {
      builder.addAllTargetMasterAddresses(this.targetMasterAddresses);
    }
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "IsAlterXClusterReplicationDone";
  }

  @Override
  Pair<IsAlterXClusterReplicationDoneResponse, Object> deserialize(
        CallResponse callResponse, String tsUUID) throws Exception {
    final MasterReplicationOuterClass.IsAlterXClusterReplicationDoneResponsePB.Builder
      builder =
        MasterReplicationOuterClass.IsAlterXClusterReplicationDoneResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);
    final MasterErrorPB error = builder.hasError() ? builder.getError() : null;
    final boolean done = builder.getDone();
    final AppStatusPB replicationError =
        builder.hasReplicationError() ? builder.getReplicationError() : null;


    IsAlterXClusterReplicationDoneResponse response =
        new IsAlterXClusterReplicationDoneResponse(
          deadlineTracker.getElapsedMillis(),
          tsUUID,
          done,
          replicationError,
          error);
    return new Pair<>(response, error);
  }
}
