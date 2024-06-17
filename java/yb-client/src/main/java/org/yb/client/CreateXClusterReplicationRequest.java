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
public class CreateXClusterReplicationRequest extends YRpc<CreateXClusterReplicationResponse> {
  private final String replicationGroupId;
  private final Set<HostPortPB> targetMasterAddresses;

  public CreateXClusterReplicationRequest(
      YBTable table, String replicationGroupId, Set<HostPortPB> targetMasterAddresses) {
    super(table);
    this.replicationGroupId = replicationGroupId;
    this.targetMasterAddresses = targetMasterAddresses;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterReplicationOuterClass.CreateXClusterReplicationRequestPB.Builder builder =
        MasterReplicationOuterClass.CreateXClusterReplicationRequestPB.newBuilder();
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
    return "CreateXClusterReplication";
  }

  @Override
  Pair<CreateXClusterReplicationResponse, Object> deserialize(
        CallResponse callResponse, String tsUUID) throws Exception {
    final MasterReplicationOuterClass.CreateXClusterReplicationResponsePB.Builder builder =
        MasterReplicationOuterClass.CreateXClusterReplicationResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);
    final MasterErrorPB error = builder.hasError() ? builder.getError() : null;


    CreateXClusterReplicationResponse response =
        new CreateXClusterReplicationResponse(
          deadlineTracker.getElapsedMillis(),
          tsUUID,
          error);
    return new Pair<>(response, error);
  }
}
