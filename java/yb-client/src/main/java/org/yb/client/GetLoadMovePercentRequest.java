// Copyright (c) YugaByte, Inc.

package org.yb.client;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import org.jboss.netty.buffer.ChannelBuffer;

import org.yb.annotations.InterfaceAudience;
import org.yb.Common.HostPortPB;
import org.yb.consensus.Consensus;
import org.yb.consensus.Metadata;
import org.yb.consensus.Metadata.RaftPeerPB;
import org.yb.master.Master;
import org.yb.util.Pair;

import java.util.ArrayList;
import java.util.List;

@InterfaceAudience.Public
class GetLoadMovePercentRequest extends YRpc<GetLoadMovePercentResponse> {
  public GetLoadMovePercentRequest(YBTable masterTable) {
    super(masterTable);
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final Master.GetLoadMovePercentRequestPB.Builder builder =
      Master.GetLoadMovePercentRequestPB.newBuilder();

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() { return "GetLoadMoveCompletion"; }

  @Override
  Pair<GetLoadMovePercentResponse, Object> deserialize(
      CallResponse callResponse,
      String masterUUID) throws Exception {
    final Master.GetLoadMovePercentResponsePB.Builder respBuilder =
      Master.GetLoadMovePercentResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    GetLoadMovePercentResponse response =
      new GetLoadMovePercentResponse(
          deadlineTracker.getElapsedMillis(),
          masterUUID,
          hasErr ? 0 : respBuilder.getPercent(),
          hasErr ? respBuilder.getErrorBuilder().build() : null);
    return new Pair<GetLoadMovePercentResponse, Object>(response,
                                                        hasErr ? respBuilder.getError() : null);
  }
}
