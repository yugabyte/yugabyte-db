package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;

import java.util.List;

public class ListCDCStreamsRequest extends YRpc<ListCDCStreamsResponse> {
  private final String tableId;
  private final String namespaceId;
  private final MasterReplicationOuterClass.IdTypePB idType;

  ListCDCStreamsRequest(YBTable table,
                        String tableId,
                        String namespaceId,
                        MasterReplicationOuterClass.IdTypePB idType) {
    super(table);
    this.tableId = tableId;
    this.namespaceId = namespaceId;
    this.idType = idType;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();

    final MasterReplicationOuterClass.ListCDCStreamsRequestPB.Builder builder =
      MasterReplicationOuterClass.ListCDCStreamsRequestPB.newBuilder();
    if (tableId != null) {
      builder.setTableId(this.tableId);
    }
    if (namespaceId != null) {
      builder.setNamespaceId(this.namespaceId);
    }
    if (idType != null) {
      builder.setIdType(this.idType);
    }

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "ListCDCStreams";
  }

  @Override
  Pair<ListCDCStreamsResponse, Object> deserialize(
    CallResponse callResponse, String tsUUID) throws Exception {
    final MasterReplicationOuterClass.ListCDCStreamsResponsePB.Builder builder =
      MasterReplicationOuterClass.ListCDCStreamsResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final MasterTypes.MasterErrorPB error = builder.hasError() ? builder.getError() : null;
    final List<MasterReplicationOuterClass.CDCStreamInfoPB> streams = builder.getStreamsList();

    ListCDCStreamsResponse response =
        new ListCDCStreamsResponse(deadlineTracker.getElapsedMillis(), tsUUID, error, streams);

    return new Pair<>(response, error);
  }
}
