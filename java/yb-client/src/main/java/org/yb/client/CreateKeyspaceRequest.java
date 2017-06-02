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
class CreateKeyspaceRequest extends YRpc<CreateKeyspaceResponse> {
  private String name;

  public CreateKeyspaceRequest(YBTable masterTable, String name) {
    super(masterTable);
    this.name = name;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final Master.CreateNamespaceRequestPB.Builder builder =
      Master.CreateNamespaceRequestPB.newBuilder();
    builder.setName(this.name);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "CreateNamespace";
  }

  @Override
  Pair<CreateKeyspaceResponse, Object> deserialize(CallResponse callResponse,
                                                   String masterUUID) throws Exception {
    final Master.CreateNamespaceResponsePB.Builder respBuilder =
        Master.CreateNamespaceResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    CreateKeyspaceResponse response =
        new CreateKeyspaceResponse(deadlineTracker.getElapsedMillis(), masterUUID,
                                   hasErr ? respBuilder.getErrorBuilder().build() : null);
    return new Pair<CreateKeyspaceResponse, Object>(response,
                                                    hasErr ? respBuilder.getError() : null);
  }
}
