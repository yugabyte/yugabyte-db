package org.yb.client;

import com.google.protobuf.Message;
import java.util.UUID;
import org.jboss.netty.buffer.ChannelBuffer;
import org.yb.WireProtocol.AppStatusPB;
import org.yb.master.Master;
import org.yb.util.Pair;

public class IsCreateXClusterReplicationDoneRequest
  extends YRpc<IsCreateXClusterReplicationDoneResponse> {

  private final UUID sourceUniverseUUID;

  IsCreateXClusterReplicationDoneRequest(YBTable table, UUID sourceUniverseUUID) {
    super(table);
    this.sourceUniverseUUID = sourceUniverseUUID;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();

    final Master.IsSetupUniverseReplicationDoneRequestPB.Builder builder =
      Master.IsSetupUniverseReplicationDoneRequestPB.newBuilder()
        .setProducerId(sourceUniverseUUID.toString());

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
  Pair<IsCreateXClusterReplicationDoneResponse, Object> deserialize(
    CallResponse callResponse, String tsUUID) throws Exception {
    final Master.IsSetupUniverseReplicationDoneResponsePB.Builder builder =
      Master.IsSetupUniverseReplicationDoneResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final Master.MasterErrorPB error = builder.hasError() ? builder.getError() : null;
    final boolean done = builder.hasDone() ? builder.getDone() : false;
    final AppStatusPB replicationError =
      builder.hasReplicationError() ? builder.getReplicationError() : null;

    IsCreateXClusterReplicationDoneResponse response =
      new IsCreateXClusterReplicationDoneResponse(
        deadlineTracker.getElapsedMillis(),
        tsUUID,
        error,
        done,
        replicationError);

    return new Pair<>(response, error);
  }
}
