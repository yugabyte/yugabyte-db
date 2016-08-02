// Copyright (c) YugaByte, Inc.

package org.yb.client;

import com.google.protobuf.Message;
import org.yb.consensus.Metadata;
import org.yb.WireProtocol;
import org.yb.annotations.InterfaceAudience;
import org.yb.util.Pair;
import org.jboss.netty.buffer.ChannelBuffer;
import org.yb.server.ServerBase;

@InterfaceAudience.Public
class PingRequest extends YRpc<PingResponse> {
  public PingRequest() {
    super(null);
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final ServerBase.PingRequestPB.Builder builder =
      ServerBase.PingRequestPB.newBuilder();
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return GENERIC_SERVICE_NAME; }

  @Override
  String method() {
    return "Ping";
  }

  @Override
  Pair<PingResponse, Object> deserialize(CallResponse callResponse,
                                         String uuid) throws Exception {
    final ServerBase.PingResponsePB.Builder respBuilder = ServerBase.PingResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    PingResponse response = new PingResponse(deadlineTracker.getElapsedMillis(), uuid);

    return new Pair<PingResponse, Object>(response, null);
  }
}
