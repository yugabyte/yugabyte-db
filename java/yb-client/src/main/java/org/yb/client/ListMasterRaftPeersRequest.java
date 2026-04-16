// Copyright (c) YugabyteDB, Inc.
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

import com.google.common.net.HostAndPort;
import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import java.util.ArrayList;
import java.util.List;
import org.yb.CommonNet.HostPortPB;
import org.yb.annotations.InterfaceAudience;
import org.yb.consensus.Metadata;
import org.yb.master.MasterClusterOuterClass;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;
import org.yb.util.PeerInfo;

@InterfaceAudience.Private
class ListMasterRaftPeersRequest extends YRpc<ListMasterRaftPeersResponse> {
  public ListMasterRaftPeersRequest(YBTable masterTable) {
    super(masterTable);
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterClusterOuterClass.ListMasterRaftPeersRequestPB.Builder builder =
      MasterClusterOuterClass.ListMasterRaftPeersRequestPB.newBuilder();
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "ListMasterRaftPeers";
  }

  @Override
  Pair<ListMasterRaftPeersResponse, Object> deserialize(CallResponse callResponse,
                                                String masterUUID) throws Exception {
    final MasterClusterOuterClass.ListMasterRaftPeersResponsePB.Builder respBuilder =
      MasterClusterOuterClass.ListMasterRaftPeersResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    MasterTypes.MasterErrorPB error = null;
    List<PeerInfo> peersList = new ArrayList<>();
    boolean hasErr = respBuilder.hasError();
    if (!hasErr) {
      for (Metadata.RaftPeerPB entry : respBuilder.getMastersList()) {
        PeerInfo peerInfo = new PeerInfo();
        List<HostAndPort> addresses = new ArrayList<>();
        for (HostPortPB hostPortPB : entry.getLastKnownPrivateAddrList()) {
          addresses.add(HostAndPort.fromParts(hostPortPB.getHost(), hostPortPB.getPort()));
        }
        List<HostAndPort> broadcastAddrs = new ArrayList<>();
        for (HostPortPB hostPortPB : entry.getLastKnownBroadcastAddrList()) {
          broadcastAddrs.add(HostAndPort.fromParts(hostPortPB.getHost(), hostPortPB.getPort()));
        }
        peerInfo.setLastKnownPrivateIps(addresses);
        peerInfo.setLastKnownBroadcastIps(broadcastAddrs);
        peerInfo.setMemberType(entry.getMemberType());
        if (entry.hasPermanentUuid()) {
          peerInfo.setUuid(entry.getPermanentUuid().toString("utf8"));
        }
        peersList.add(peerInfo);
      }
    } else {
      error = respBuilder.getError();
    }
    ListMasterRaftPeersResponse response = new ListMasterRaftPeersResponse(
        deadlineTracker.getElapsedMillis(), masterUUID, error, peersList);

    return new Pair<>(response, hasErr ? respBuilder.getError() : null);
  }
}
