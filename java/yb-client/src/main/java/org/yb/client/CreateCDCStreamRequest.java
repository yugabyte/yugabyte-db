package org.yb.client;

import com.google.protobuf.Message;
import org.jboss.netty.buffer.ChannelBuffer;
import org.yb.cdc.CdcService.CreateCDCStreamRequestPB;
import org.yb.cdc.CdcService.CreateCDCStreamResponsePB;
import org.yb.util.Pair;

public class CreateCDCStreamRequest extends YRpc<CreateCDCStreamResponse> {
  private final String tableId;

  public CreateCDCStreamRequest(YBTable masterTable, String tableId) {
    super(masterTable);
    this.tableId = tableId;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final CreateCDCStreamRequestPB.Builder builder = CreateCDCStreamRequestPB.newBuilder();
    builder.setTableId(this.tableId);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return CDC_SERVICE_NAME; }

  @Override
  String method() {
    return "CreateCDCStream";
  }

  @Override
  Pair<CreateCDCStreamResponse, Object> deserialize(
          CallResponse callResponse, String uuid) throws Exception {
    final CreateCDCStreamResponsePB.Builder respBuilder = CreateCDCStreamResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);

    CreateCDCStreamResponse response = new CreateCDCStreamResponse(
            deadlineTracker.getElapsedMillis(), uuid, respBuilder.getStreamId().toStringUtf8());
    return new Pair<CreateCDCStreamResponse, Object>(
            response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}
