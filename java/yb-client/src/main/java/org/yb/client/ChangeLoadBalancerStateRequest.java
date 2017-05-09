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
class ChangeLoadBalancerStateRequest extends YRpc<ChangeLoadBalancerStateResponse> {
  private boolean isEnable;

  public ChangeLoadBalancerStateRequest(
      YBTable masterTable, boolean isEnable) {
    super(masterTable);
    this.isEnable = isEnable;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final Master.ChangeLoadBalancerStateRequestPB.Builder builder =
      Master.ChangeLoadBalancerStateRequestPB.newBuilder();
    builder.setIsEnabled(isEnable);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "ChangeLoadBalancerState";
  }

  @Override
  Pair<ChangeLoadBalancerStateResponse, Object> deserialize(CallResponse callResponse,
                                                 String masterUUID) throws Exception {
    final Master.ChangeLoadBalancerStateResponsePB.Builder respBuilder =
        Master.ChangeLoadBalancerStateResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    ChangeLoadBalancerStateResponse response =
      new ChangeLoadBalancerStateResponse(
          deadlineTracker.getElapsedMillis(),
          masterUUID,
          hasErr ? respBuilder.getErrorBuilder().build() : null);
    return new Pair<ChangeLoadBalancerStateResponse, Object>(
        response,
        hasErr ? respBuilder.getError() : null);
  }
}
