// Copyright (c) YugabyteDB, Inc.

package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.consensus.Consensus.GetConsensusStateRequestPB;
import org.yb.consensus.Consensus.GetConsensusStateResponsePB;
import org.yb.util.Pair;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;

import io.netty.buffer.ByteBuf;

@InterfaceAudience.Public
public class GetConsensusStateRequest extends YRpc<GetConsensusStateResponse> {

  private final String tablet_id;

  public GetConsensusStateRequest(String tablet_id) {
    super(null);
    this.tablet_id = tablet_id;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final GetConsensusStateRequestPB.Builder builder = GetConsensusStateRequestPB.newBuilder();
    builder.setTabletId(ByteString.copyFromUtf8(tablet_id));
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return CONSENSUS_SERVICE_NAME;
  }

  @Override
  String method() {
    return "GetConsensusState";
  }

  @Override
  Pair<GetConsensusStateResponse, Object> deserialize(
      CallResponse callResponse, String tsUUID) throws Exception {
    final GetConsensusStateResponsePB.Builder builder = GetConsensusStateResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), builder);
    GetConsensusStateResponse response = new GetConsensusStateResponse(
        deadlineTracker.getElapsedMillis(), tsUUID,
        builder.getCstate(), builder.getError(), builder.getLeaderLeaseStatus());
    return new Pair(response, null);
  }

}
