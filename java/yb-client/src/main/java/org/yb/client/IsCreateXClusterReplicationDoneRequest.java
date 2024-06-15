// Copyright (c) YugaByte, Inc.

package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import java.util.Set;
import org.yb.CommonNet.HostPortPB;
import org.yb.WireProtocol.AppStatusPB;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.util.Pair;


@InterfaceAudience.Public
public class IsCreateXClusterReplicationDoneRequest
    extends YRpc<IsCreateXClusterReplicationDoneResponse> {
  private final String replicationGroupId;
  private final Set<HostPortPB> targetMasterAddresses;

  public IsCreateXClusterReplicationDoneRequest(
      YBTable table, String replicationGroupId, Set<HostPortPB> targetMasterAddresses) {
    super(table);
    this.replicationGroupId = replicationGroupId;
    this.targetMasterAddresses = targetMasterAddresses;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterReplicationOuterClass.IsCreateXClusterReplicationDoneRequestPB.Builder builder =
        MasterReplicationOuterClass.IsCreateXClusterReplicationDoneRequestPB.newBuilder();
    builder.setReplicationGroupId(this.replicationGroupId);
    builder.addAllTargetMasterAddresses(this.targetMasterAddresses);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "IsCreateXClusterReplicationDone";
  }

  @Override
  Pair<IsCreateXClusterReplicationDoneResponse, Object> deserialize(
        CallResponse callResponse, String tsUUID) throws Exception {
    final MasterReplicationOuterClass.IsCreateXClusterReplicationDoneResponsePB.Builder builder =
        MasterReplicationOuterClass.IsCreateXClusterReplicationDoneResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);
    final MasterErrorPB error = builder.hasError() ? builder.getError() : null;
    final boolean done = builder.getDone();
    final AppStatusPB replicationError =
        builder.hasReplicationError() ? builder.getReplicationError() : null;

    IsCreateXClusterReplicationDoneResponse response =
        new IsCreateXClusterReplicationDoneResponse(
          deadlineTracker.getElapsedMillis(),
          tsUUID,
          done,
          replicationError,
          error);
    return new Pair<>(response, error);
  }
}
