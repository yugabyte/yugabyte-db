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

@InterfaceAudience.Private
class ChangeConfigRequest extends YRpc<ChangeConfigResponse> {

  public enum ServerType {
    TSERVER,
    MASTER
  }

  private final String leader_uuid;
  private final String tablet_id;
  private final Consensus.ChangeConfigType changeType;
  private final int port;
  private final String host;
  private final String uuid;
  private final ServerType serverType;

  public ChangeConfigRequest(
      String leader_uuid, YBTable masterTable, String host, int port, String uuid, boolean isAdd) {
    super(masterTable);
    this.tablet_id = YBClient.getMasterTabletId();
    this.uuid = uuid;
    this.host = host;
    this.port = port;
    this.leader_uuid = leader_uuid;
    this.changeType = isAdd ? Consensus.ChangeConfigType.ADD_SERVER
                            : Consensus.ChangeConfigType.REMOVE_SERVER;
    this.serverType = ServerType.MASTER;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final Consensus.ChangeConfigRequestPB.Builder builder =
      Consensus.ChangeConfigRequestPB.newBuilder();

    if (serverType != ServerType.MASTER) {
      throw new IllegalArgumentException("Unexpected server type " + serverType);
    }

    HostPortPB.Builder hpb =
        HostPortPB.newBuilder()
                  .setPort(port)
                  .setHost(host);
    RaftPeerPB.Builder pbb =
        RaftPeerPB.newBuilder()
                  .setPermanentUuid(ByteString.copyFromUtf8(uuid))
                  .setLastKnownAddr(hpb.build());
    builder.setType(this.changeType)
           .setTabletId(ByteString.copyFromUtf8(tablet_id))
           .setDestUuid(ByteString.copyFromUtf8(leader_uuid))
           .setServer(pbb.build());

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return CONSENSUS_SERVICE_NAME; }

  @Override
  String method() {
    return "ChangeConfig";
  }

  @Override
  Pair<ChangeConfigResponse, Object> deserialize(CallResponse callResponse,
                                                 String masterUUID) throws Exception {
    final Consensus.ChangeConfigResponsePB.Builder respBuilder =
      Consensus.ChangeConfigResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    ChangeConfigResponse response =
      new ChangeConfigResponse(deadlineTracker.getElapsedMillis(),
                               masterUUID,
                               hasErr ? respBuilder.getErrorBuilder().build() : null);
    return new Pair<ChangeConfigResponse, Object>(response, hasErr ? respBuilder.getError() : null);
  }
}
