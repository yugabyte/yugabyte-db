// Copyright (c) YugaByte, Inc.

package org.yb.client;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import org.jboss.netty.buffer.ChannelBuffer;

import org.yb.consensus.Consensus;
import org.yb.master.Master;
import org.yb.util.Pair;

class IsLeaderReadyForChangeConfigRequest extends YRpc<IsLeaderReadyForChangeConfigResponse> {
  private final String leaderUuid;
  private final String tabletId;

  public IsLeaderReadyForChangeConfigRequest(YBTable masterTable,
                                             String leaderUuid,
                                             String tabletId) {
    super(masterTable);
    this.leaderUuid = leaderUuid;
    this.tabletId = tabletId;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final Consensus.IsLeaderReadyForChangeConfigRequestPB.Builder builder =
      Consensus.IsLeaderReadyForChangeConfigRequestPB.newBuilder();
    builder.setTabletId(ByteString.copyFromUtf8(tabletId))
           .setDestUuid(ByteString.copyFromUtf8(leaderUuid));
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return CONSENSUS_SERVICE_NAME; }

  @Override
  String method() {
    return "IsLeaderReadyForChangeConfig";
  }

  @Override
  Pair<IsLeaderReadyForChangeConfigResponse, Object> deserialize(CallResponse callResponse,
                                                                 String masterUUID) throws Exception {
    final Consensus.IsLeaderReadyForChangeConfigResponsePB.Builder respBuilder =
      Consensus.IsLeaderReadyForChangeConfigResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    IsLeaderReadyForChangeConfigResponse response =
      new IsLeaderReadyForChangeConfigResponse(deadlineTracker.getElapsedMillis(),
                                               masterUUID,
                                               hasErr ? respBuilder.getErrorBuilder().build() : null,
                                               hasErr ? false : respBuilder.getIsReady());
    return new Pair<IsLeaderReadyForChangeConfigResponse, Object>(response,
    		                                                      hasErr ? respBuilder.getError() : null);
  }
}
