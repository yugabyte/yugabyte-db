// Copyright (c) YugabyteDB, Inc.

package org.yb.client;

import java.util.List;

import org.yb.tserver.Tserver.ListTabletsRequestPB;
import org.yb.tserver.Tserver.ListTabletsResponsePB;
import org.yb.tserver.Tserver.ListTabletsResponsePB.StatusAndSchemaPB;
import org.yb.util.Pair;

import com.google.protobuf.Message;

import io.netty.buffer.ByteBuf;

public class ListTabletsRequest extends YRpc<ListTabletsResponse> {

  public ListTabletsRequest() {
    super(null);
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final ListTabletsRequestPB.Builder builder = ListTabletsRequestPB.newBuilder();
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return TABLET_SERVER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "ListTablets";
  }

  @Override
  Pair<ListTabletsResponse, Object> deserialize(
      CallResponse callResponse, String tsUUID) throws Exception {
    final ListTabletsResponsePB.Builder respBuilder = ListTabletsResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);

    List<StatusAndSchemaPB> statusAndSchemaPBs = respBuilder.getStatusAndSchemaList();

    ListTabletsResponse response = new ListTabletsResponse(deadlineTracker.getElapsedMillis(),
        tsUUID, statusAndSchemaPBs, respBuilder.getError());

    return new Pair<ListTabletsResponse, Object>(response, null);
  }

}
