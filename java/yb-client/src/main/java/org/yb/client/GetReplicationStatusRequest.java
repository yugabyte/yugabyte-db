package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import javax.annotation.Nullable;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.util.Pair;

public class GetReplicationStatusRequest extends YRpc<GetReplicationStatusResponse> {

  private final String replicationGroupName;

  GetReplicationStatusRequest(YBTable table, @Nullable String replicationGroupName) {
    super(table);
    this.replicationGroupName = replicationGroupName;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterReplicationOuterClass.GetReplicationStatusRequestPB.Builder builder =
        MasterReplicationOuterClass.GetReplicationStatusRequestPB.newBuilder();
    if (this.replicationGroupName != null) {
      builder.setReplicationGroupId(this.replicationGroupName);
    }
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "GetReplicationStatus";
  }

  @Override
  Pair<GetReplicationStatusResponse, Object> deserialize(
    CallResponse callResponse, String tsUUID) throws Exception {
    final MasterReplicationOuterClass.GetReplicationStatusResponsePB.Builder builder =
        MasterReplicationOuterClass.GetReplicationStatusResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final MasterErrorPB error = builder.hasError() ? builder.getError() : null;

    GetReplicationStatusResponse response =
        new GetReplicationStatusResponse(
            deadlineTracker.getElapsedMillis(), tsUUID, error, builder.getStatusesList());

    return new Pair<>(response, error);
  }
}
