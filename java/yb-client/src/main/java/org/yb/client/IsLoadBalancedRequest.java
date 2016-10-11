// Copyright (c) YugaByte, Inc.

package org.yb.client;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import org.jboss.netty.buffer.ChannelBuffer;

import org.yb.annotations.InterfaceAudience;
import org.yb.master.Master;
import org.yb.util.Pair;

import java.util.ArrayList;
import java.util.List;

@InterfaceAudience.Public
class IsLoadBalancedRequest extends YRpc<IsLoadBalancedResponse> {
  public IsLoadBalancedRequest(YBTable masterTable) {
    super(masterTable);
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final Master.IsLoadBalancedRequestPB.Builder builder =
      Master.IsLoadBalancedRequestPB.newBuilder();

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() { return "IsLoadBalanced"; }

  @Override
  Pair<IsLoadBalancedResponse, Object> deserialize(
      CallResponse callResponse,
      String masterUUID) throws Exception {
    final Master.IsLoadBalancedResponsePB.Builder respBuilder =
      Master.IsLoadBalancedResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    IsLoadBalancedResponse response =
      new IsLoadBalancedResponse(deadlineTracker.getElapsedMillis(),
                                 masterUUID,
                                 hasErr ? respBuilder.getErrorBuilder().build() : null);
    return new Pair<IsLoadBalancedResponse, Object>(response,
                                                    hasErr ? respBuilder.getError() : null);
  }
}
