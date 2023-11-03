package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.util.Pair;

public class GetXClusterSafeTimeRequest extends YRpc<GetXClusterSafeTimeResponse> {

  GetXClusterSafeTimeRequest(YBTable table) {
    super(table);
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterReplicationOuterClass.GetXClusterSafeTimeRequestPB.Builder builder =
        MasterReplicationOuterClass.GetXClusterSafeTimeRequestPB.newBuilder();
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "GetXClusterSafeTime";
  }

  @Override
  Pair<GetXClusterSafeTimeResponse, Object> deserialize(
    CallResponse callResponse, String tsUUID) throws Exception {
    final MasterReplicationOuterClass.GetXClusterSafeTimeResponsePB.Builder builder =
        MasterReplicationOuterClass.GetXClusterSafeTimeResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final MasterErrorPB error = builder.hasError() ? builder.getError() : null;

    GetXClusterSafeTimeResponse response =
        new GetXClusterSafeTimeResponse(
            deadlineTracker.getElapsedMillis(), tsUUID, error, builder.getNamespaceSafeTimesList());

    return new Pair<>(response, error);
  }
}
