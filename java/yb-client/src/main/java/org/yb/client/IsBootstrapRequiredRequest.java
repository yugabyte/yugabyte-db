package org.yb.client;

import com.google.protobuf.Message;
import java.util.List;
import org.jboss.netty.buffer.ChannelBuffer;
import org.yb.cdc.CdcService;
import org.yb.util.Pair;

public class IsBootstrapRequiredRequest extends YRpc<IsBootstrapRequiredResponse> {

  private final List<String> tabletIds;
  private final String streamId;

  IsBootstrapRequiredRequest(YBTable table, List<String> tabletIds, String streamId) {
    super(table);
    this.tabletIds = tabletIds;
    this.streamId = streamId;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();

    final CdcService.IsBootstrapRequiredRequestPB.Builder builder =
      CdcService.IsBootstrapRequiredRequestPB.newBuilder();
    builder.addAllTabletIds(this.tabletIds);
    if (this.streamId != null) {
      builder.setStreamId(this.streamId);
    }

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return CDC_SERVICE_NAME;
  }

  @Override
  String method() {
    return "IsBootstrapRequired";
  }

  @Override
  Pair<IsBootstrapRequiredResponse, Object> deserialize(
    CallResponse callResponse, String tsUUID) throws Exception {
    final CdcService.IsBootstrapRequiredResponsePB.Builder builder =
      CdcService.IsBootstrapRequiredResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final CdcService.CDCErrorPB error = builder.hasError() ? builder.getError() : null;
    final boolean bootstrapRequired = builder.getBootstrapRequired();

    IsBootstrapRequiredResponse response =
      new IsBootstrapRequiredResponse(
        deadlineTracker.getElapsedMillis(), tsUUID, error, bootstrapRequired);

    return new Pair<>(response, error);
  }
}
