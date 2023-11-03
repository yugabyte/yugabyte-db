package org.yb.client;

import com.google.protobuf.Message;
import java.util.UUID;
import io.netty.buffer.ByteBuf;
import org.yb.WireProtocol.AppStatusPB;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;

public class IsSetupUniverseReplicationDoneRequest
  extends YRpc<IsSetupUniverseReplicationDoneResponse> {

  private final String producerId;

  IsSetupUniverseReplicationDoneRequest(YBTable table, String producerId) {
    super(table);
    this.producerId = producerId;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();

    final MasterReplicationOuterClass.IsSetupUniverseReplicationDoneRequestPB.Builder builder =
      MasterReplicationOuterClass.IsSetupUniverseReplicationDoneRequestPB.newBuilder()
        .setProducerId(producerId.toString());

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "IsSetupUniverseReplicationDone";
  }

  @Override
  Pair<IsSetupUniverseReplicationDoneResponse, Object> deserialize(
    CallResponse callResponse, String tsUUID) throws Exception {
    final MasterReplicationOuterClass.IsSetupUniverseReplicationDoneResponsePB.Builder builder =
      MasterReplicationOuterClass.IsSetupUniverseReplicationDoneResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final MasterTypes.MasterErrorPB error = builder.hasError() ? builder.getError() : null;
    final boolean done = builder.hasDone() ? builder.getDone() : false;
    final AppStatusPB replicationError =
      builder.hasReplicationError() ? builder.getReplicationError() : null;

    IsSetupUniverseReplicationDoneResponse response =
      new IsSetupUniverseReplicationDoneResponse(
        deadlineTracker.getElapsedMillis(),
        tsUUID,
        error,
        done,
        replicationError);

    return new Pair<>(response, error);
  }
}
