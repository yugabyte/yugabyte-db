package org.yb.client;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import java.util.List;
import java.util.Objects;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.util.Pair;

public class WaitForReplicationDrainRequest extends YRpc<WaitForReplicationDrainResponse> {

  private final List<String> streamIds;
  private final Long targetTime;

  WaitForReplicationDrainRequest(YBTable table, List<String> streamIds, @Nullable Long targetTime) {
    super(table);
    this.streamIds = streamIds;
    this.targetTime = targetTime;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterReplicationOuterClass.WaitForReplicationDrainRequestPB.Builder builder =
        MasterReplicationOuterClass.WaitForReplicationDrainRequestPB.newBuilder();
    builder.addAllStreamIds(
        this.streamIds.stream().map(ByteString::copyFromUtf8).collect(Collectors.toList()));
    if (Objects.nonNull(this.targetTime)){
      builder.setTargetTime(this.targetTime);
    }
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "WaitForReplicationDrain";
  }

  @Override
  Pair<WaitForReplicationDrainResponse, Object> deserialize(
    CallResponse callResponse, String tsUUID) throws Exception {
    final MasterReplicationOuterClass.WaitForReplicationDrainResponsePB.Builder builder =
        MasterReplicationOuterClass.WaitForReplicationDrainResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final MasterErrorPB error = builder.hasError() ? builder.getError() : null;

    WaitForReplicationDrainResponse response =
        new WaitForReplicationDrainResponse(
            deadlineTracker.getElapsedMillis(),
            tsUUID,
            error,
            builder.getUndrainedStreamInfoList());

    return new Pair<>(response, error);
  }
}
