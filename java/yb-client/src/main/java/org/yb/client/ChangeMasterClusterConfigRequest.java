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
class ChangeMasterClusterConfigRequest extends YRpc<ChangeMasterClusterConfigResponse> {
  private Master.SysClusterConfigEntryPB clusterConfig;

  public ChangeMasterClusterConfigRequest(
      YBTable masterTable, Master.SysClusterConfigEntryPB config) {
    super(masterTable);
    clusterConfig = config;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final Master.ChangeMasterClusterConfigRequestPB.Builder builder =
      Master.ChangeMasterClusterConfigRequestPB.newBuilder();
    builder.setClusterConfig(clusterConfig);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "ChangeMasterClusterConfig";
  }

  @Override
  Pair<ChangeMasterClusterConfigResponse, Object> deserialize(CallResponse callResponse,
                                                 String masterUUID) throws Exception {
    final Master.ChangeMasterClusterConfigResponsePB.Builder respBuilder =
      Master.ChangeMasterClusterConfigResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    ChangeMasterClusterConfigResponse response =
      new ChangeMasterClusterConfigResponse(deadlineTracker.getElapsedMillis(),
                               masterUUID,
                               hasErr ? respBuilder.getErrorBuilder().build() : null);
    return new Pair<ChangeMasterClusterConfigResponse, Object>(response, hasErr ? respBuilder.getError() : null);
  }
}
