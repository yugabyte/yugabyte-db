package org.yb.client;

import com.google.protobuf.Message;
import org.apache.log4j.LogMF;
import org.jboss.netty.buffer.ChannelBuffer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.cdc.CdcService.CreateCDCStreamRequestPB;
import org.yb.cdc.CdcService.CreateCDCStreamResponsePB;
import org.yb.cdc.CdcService;
import org.yb.util.Pair;

public class CreateCDCStreamRequest extends YRpc<CreateCDCStreamResponse> {
  public static final Logger LOG = LoggerFactory.getLogger(CreateCDCStreamRequest.class);

  private final String tableId;
  private final String namespaceName;
  private final CdcService.CDCRequestSource source_type;
  private final CdcService.CDCRecordFormat record_format;
  private final CdcService.CDCCheckpointType checkpoint_type;

  public CreateCDCStreamRequest(YBTable masterTable, String tableId,
                                String namespaceName, String format,
                                String checkpointType) {
    super(masterTable);
    this.tableId = tableId;
    this.namespaceName = namespaceName;
    this.source_type = CdcService.CDCRequestSource.CDCSDK;
    if (format.equalsIgnoreCase("PROTO"))
      this.record_format = CdcService.CDCRecordFormat.PROTO;
    else {
      this.record_format = CdcService.CDCRecordFormat.JSON;
    }
    if (checkpointType.equalsIgnoreCase("EXPLICIT")) {
      this.checkpoint_type = CdcService.CDCCheckpointType.EXPLICIT;
    }
    else {
      this.checkpoint_type = CdcService.CDCCheckpointType.IMPLICIT;
    }
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final CreateCDCStreamRequestPB.Builder builder = CreateCDCStreamRequestPB.newBuilder();
    if (namespaceName == null)
      builder.setTableId(this.tableId);
    builder.setNamespaceName(this.namespaceName);
    builder.setSourceType(this.source_type);
    builder.setRecordFormat(this.record_format);
    builder.setCheckpointType(this.checkpoint_type);
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
            deadlineTracker.getElapsedMillis(), uuid, respBuilder.getDbStreamId().toStringUtf8());
    return new Pair<CreateCDCStreamResponse, Object>(
            response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}
