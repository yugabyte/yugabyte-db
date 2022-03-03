package org.yb.client;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import org.jboss.netty.buffer.ChannelBuffer;
import org.yb.cdc.CdcService;
import org.yb.util.Pair;

public class GetCheckpointRequest extends YRpc<GetCheckpointResponse>{
  private String streamId;
  private String tabletId;

  public GetCheckpointRequest(YBTable table, String streamId, String tabletId) {
    super(table);
    this.streamId = streamId;
    this.tabletId = tabletId;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final CdcService.GetCheckpointRequestPB.Builder builder = CdcService
    .GetCheckpointRequestPB.newBuilder();
    builder.setStreamId(ByteString.copyFromUtf8(this.streamId));
    builder.setTabletId(ByteString.copyFromUtf8(this.tabletId));

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return CDC_SERVICE_NAME; }

  @Override
  String method() {
    return "GetCheckpoint";
  }

  @Override
  Pair<GetCheckpointResponse, Object> deserialize(CallResponse callResponse,
                                                  String uuid) throws Exception {
    final CdcService.GetCheckpointResponsePB.Builder respBuilder =
      CdcService.GetCheckpointResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);

    GetCheckpointResponse response =
      new GetCheckpointResponse(deadlineTracker.getElapsedMillis(), uuid,
        respBuilder.getCheckpoint().getOpId().getIndex(),
    respBuilder.getCheckpoint().getOpId().getTerm());
    return new Pair<GetCheckpointResponse, Object>(
      response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}
