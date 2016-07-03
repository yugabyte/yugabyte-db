// Copyright (c) YugaByte, Inc.

package org.yb.client;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import org.jboss.netty.buffer.ChannelBuffer;

import org.yb.annotations.InterfaceAudience;
import org.yb.consensus.Consensus;
import org.yb.util.Pair;

@InterfaceAudience.Private
class LeaderStepDownRequest extends YRpc<LeaderStepDownResponse> {
  private String leader_uuid;
  private String tablet_id;

  public LeaderStepDownRequest(YBTable masterTable, String leader_uuid, String tablet_id) {
    super(masterTable);
    this.leader_uuid = leader_uuid;
    this.tablet_id = tablet_id;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final Consensus.LeaderStepDownRequestPB.Builder builder =
      Consensus.LeaderStepDownRequestPB.newBuilder();
    builder.setDestUuid(ByteString.copyFromUtf8(leader_uuid));
    builder.setTabletId(ByteString.copyFromUtf8(tablet_id));
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return CONSENSUS_SERVICE_NAME; }

  @Override
  String method() {
    return "LeaderStepDown";
  }

  @Override
  Pair<LeaderStepDownResponse, Object> deserialize(CallResponse callResponse,
                                                   String masterUUID) throws Exception {
    final Consensus.LeaderStepDownResponsePB.Builder respBuilder =
      Consensus.LeaderStepDownResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();

    LeaderStepDownResponse response = new LeaderStepDownResponse(
        deadlineTracker.getElapsedMillis(),
        masterUUID,
        hasErr ? respBuilder.getErrorBuilder().build() : null);

    return new Pair<LeaderStepDownResponse, Object>(response, hasErr ? respBuilder.getError() : null);
  }
}
