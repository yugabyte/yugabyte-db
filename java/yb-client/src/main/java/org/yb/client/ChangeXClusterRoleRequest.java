package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.annotations.InterfaceAudience;
import org.yb.cdc.CdcConsumer.XClusterRole;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class ChangeXClusterRoleRequest extends YRpc<ChangeXClusterRoleResponse> {

  private final XClusterRole role;

  ChangeXClusterRoleRequest(YBTable table, XClusterRole role) {
    super(table);
    this.role = role;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    MasterReplicationOuterClass.ChangeXClusterRoleRequestPB.Builder builder =
        MasterReplicationOuterClass.ChangeXClusterRoleRequestPB.newBuilder();
    builder.setRole(role);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "ChangeXClusterRole";
  }

  @Override
  Pair<ChangeXClusterRoleResponse, Object> deserialize(
    CallResponse callResponse, String tsUUID) throws Exception {
    MasterReplicationOuterClass.ChangeXClusterRoleResponsePB.Builder builder =
        MasterReplicationOuterClass.ChangeXClusterRoleResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), builder);

    MasterErrorPB error = builder.hasError() ? builder.getError() : null;
    ChangeXClusterRoleResponse response =
        new ChangeXClusterRoleResponse(
            deadlineTracker.getElapsedMillis(), tsUUID, error);

    return new Pair<>(response, error);
  }
}
