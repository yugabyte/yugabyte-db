package org.yb.client;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import org.jboss.netty.buffer.ChannelBuffer;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.util.Pair;

public class GetDBStreamInfoRequest extends YRpc<GetDBStreamInfoResponse>{

  private final String streamId;

  GetDBStreamInfoRequest(YBTable table, String streamId) {
    super(table);
    this.streamId = streamId;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final MasterReplicationOuterClass.GetCDCDBStreamInfoRequestPB.Builder builder =
            MasterReplicationOuterClass.GetCDCDBStreamInfoRequestPB.newBuilder();
    builder.setDbStreamId(ByteString.copyFromUtf8(this.streamId));
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "GetCDCDBStreamInfo";
  }

  @Override
  Pair<GetDBStreamInfoResponse, Object> deserialize(CallResponse callResponse,
                                                    String tsUUID) throws Exception {
    final MasterReplicationOuterClass.GetCDCDBStreamInfoResponsePB.Builder respBuilder =
      MasterReplicationOuterClass.GetCDCDBStreamInfoResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);


    GetDBStreamInfoResponse response =
      new GetDBStreamInfoResponse(deadlineTracker.getElapsedMillis(), tsUUID,
        respBuilder.getTableInfoList());
    return new Pair<GetDBStreamInfoResponse, Object>(
      response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}
