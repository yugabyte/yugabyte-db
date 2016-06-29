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
class GetMasterClusterConfigRequest extends YRpc<GetMasterClusterConfigResponse> {
  public GetMasterClusterConfigRequest(YBTable masterTable) {
    super(masterTable);
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final Master.GetMasterClusterConfigRequestPB.Builder builder =
      Master.GetMasterClusterConfigRequestPB.newBuilder();

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() { return "GetMasterClusterConfig"; }

  @Override
  Pair<GetMasterClusterConfigResponse, Object> deserialize(
      CallResponse callResponse,
      String masterUUID) throws Exception {
    final Master.GetMasterClusterConfigResponsePB.Builder respBuilder =
      Master.GetMasterClusterConfigResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    GetMasterClusterConfigResponse response =
      new GetMasterClusterConfigResponse(
          deadlineTracker.getElapsedMillis(),
          masterUUID,
          respBuilder.getClusterConfig(),
          hasErr ? respBuilder.getErrorBuilder().build() : null);
    return new Pair<GetMasterClusterConfigResponse, Object>(response, hasErr ? respBuilder.getError() : null);
  }
}
