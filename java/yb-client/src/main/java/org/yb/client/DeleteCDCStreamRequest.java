package org.yb.client;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import java.nio.charset.StandardCharsets;
import java.util.Set;
import java.util.stream.Collectors;
import io.netty.buffer.ByteBuf;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;

public class DeleteCDCStreamRequest extends YRpc<DeleteCDCStreamResponse> {
  private final Set<ByteString> streamIds;
  // Default value is false.
  private final boolean ignoreErrors;
  // Default value is false.
  private final boolean forceDelete;

  DeleteCDCStreamRequest(YBTable table,
                         Set<String> streamIds,
                         boolean ignoreErrors,
                         boolean forceDelete) {
    super(table);
    this.streamIds =
        streamIds
            .stream()
            .map(streamId -> ByteString.copyFrom(streamId, StandardCharsets.UTF_8))
            .collect(Collectors.toSet());
    this.ignoreErrors = ignoreErrors;
    this.forceDelete = forceDelete;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();

    final MasterReplicationOuterClass.DeleteCDCStreamRequestPB.Builder builder =
        MasterReplicationOuterClass.DeleteCDCStreamRequestPB.newBuilder()
            .addAllStreamId(streamIds)
            .setIgnoreErrors(ignoreErrors)
            .setForceDelete(forceDelete);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "DeleteCDCStream";
  }

  @Override
  Pair<DeleteCDCStreamResponse, Object> deserialize(
    CallResponse callResponse, String tsUUID) throws Exception {
    final MasterReplicationOuterClass.DeleteCDCStreamResponsePB.Builder builder =
        MasterReplicationOuterClass.DeleteCDCStreamResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final MasterTypes.MasterErrorPB error = builder.hasError() ? builder.getError() : null;
    final Set<String> notFoundStreamIds =
        builder.getNotFoundStreamIdsList()
            .stream()
            .map(ByteString::toStringUtf8)
            .collect(Collectors.toSet());

    DeleteCDCStreamResponse response =
        new DeleteCDCStreamResponse(
            deadlineTracker.getElapsedMillis(), tsUUID, error, notFoundStreamIds);

    return new Pair<>(response, error);
  }
}
