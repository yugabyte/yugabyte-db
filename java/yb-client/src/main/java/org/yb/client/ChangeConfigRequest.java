// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.client;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.CommonNet.HostPortPB;
import org.yb.annotations.InterfaceAudience;
import org.yb.consensus.Consensus;
import org.yb.consensus.ConsensusTypes;
import org.yb.consensus.Metadata;
import org.yb.consensus.Metadata.RaftPeerPB;
import org.yb.util.Pair;

@InterfaceAudience.Private
class ChangeConfigRequest extends YRpc<ChangeConfigResponse> {

  public enum ServerType {
    TSERVER,
    MASTER
  }

  private final String tablet_id;
  private final ConsensusTypes.ChangeConfigType changeType;
  private final int port;
  private final String host;
  private final String uuid;
  private final boolean useHost;
  private final ServerType serverType;

  public ChangeConfigRequest(
      YBTable masterTable, String host, int port, String uuid, boolean isAdd, boolean useHost) {
    super(masterTable);
    this.tablet_id = YBClient.getMasterTabletId();
    this.uuid = uuid;
    this.host = host;
    this.port = port;
    this.useHost = useHost;
    this.changeType = isAdd ? ConsensusTypes.ChangeConfigType.ADD_SERVER
                            : ConsensusTypes.ChangeConfigType.REMOVE_SERVER;
    this.serverType = ServerType.MASTER;
  }

  @Override
  ByteBuf serialize(Message header) {
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
                  .addLastKnownPrivateAddr(hpb.build());

    if (uuid != null) {
      pbb.setPermanentUuid(ByteString.copyFromUtf8(uuid));
    }

    if (useHost) {
      builder.setUseHost(true);
    }

    if (this.changeType == ConsensusTypes.ChangeConfigType.ADD_SERVER) {
      pbb.setMemberType(Metadata.PeerMemberType.PRE_VOTER);
    }

    builder.setType(this.changeType)
           .setTabletId(ByteString.copyFromUtf8(tablet_id))
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
