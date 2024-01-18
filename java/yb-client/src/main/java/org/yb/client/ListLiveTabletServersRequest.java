// Copyright (c) YugaByte, Inc.

package org.yb.client;

import com.google.common.net.HostAndPort;
import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import org.yb.CommonNet.CloudInfoPB;
import org.yb.WireProtocol.NodeInstancePB;
import org.yb.WireProtocol.ServerRegistrationPB;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterClusterOuterClass.ListLiveTabletServersRequestPB;
import org.yb.master.MasterClusterOuterClass.ListLiveTabletServersResponsePB;
import org.yb.util.CommonUtil;
import org.yb.util.Pair;
import org.yb.util.TabletServerInfo;

@InterfaceAudience.Public
public class ListLiveTabletServersRequest extends YRpc<ListLiveTabletServersResponse> {

  public ListLiveTabletServersRequest(YBTable masterTable) {
    super(masterTable);
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "ListLiveTabletServers";
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final ListLiveTabletServersRequestPB.Builder builder =
      ListLiveTabletServersRequestPB.newBuilder();
    return toChannelBuffer(header, builder.build());
  }

  @Override
  Pair<ListLiveTabletServersResponse, Object> deserialize(CallResponse callResponse,
                                                      String tsUUID) throws Exception {
    final ListLiveTabletServersResponsePB.Builder respBuilder =
      ListLiveTabletServersResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();

    List<TabletServerInfo> tabletServers =
      new ArrayList<TabletServerInfo>(respBuilder.getServersCount());

    if (!hasErr) {
      for (ListLiveTabletServersResponsePB.Entry entry : respBuilder.getServersList()) {
        ServerRegistrationPB common = entry.getRegistration().getCommon();
        NodeInstancePB instance = entry.getInstanceId();
        CloudInfoPB cloudInfoPB = common.getCloudInfo();
        TabletServerInfo tserverInfo = new TabletServerInfo();

        // tserverUuid() returns ByteString -> String not containing dashes.
        UUID tserverUuid = CommonUtil.convertToUUID(instance.getPermanentUuid());
        tserverInfo.setUuid(tserverUuid);

        TabletServerInfo.CloudInfo cloudInfo = new TabletServerInfo.CloudInfo();
        cloudInfo.setCloud(cloudInfoPB.getPlacementCloud());
        cloudInfo.setRegion(cloudInfoPB.getPlacementRegion());
        cloudInfo.setZone(cloudInfoPB.getPlacementZone());
        tserverInfo.setCloudInfo(cloudInfo);

        // getPlacementUuid() returns ByteString -> String containing dashes.
        UUID placementUuid = UUID.fromString(common.getPlacementUuid().toStringUtf8());
        tserverInfo.setPlacementUuid(placementUuid);
        tserverInfo.setInPrimaryCluster(!entry.getIsFromReadReplica());

        if (!common.getBroadcastAddressesList().isEmpty()) {
          HostAndPort broadcastAddress =
            HostAndPort.fromParts(
              common.getBroadcastAddressesList().get(0).getHost(),
              common.getBroadcastAddressesList().get(0).getPort());
          tserverInfo.setBroadcastAddress(broadcastAddress);
        }

        if (!common.getPrivateRpcAddressesList().isEmpty()) {
          HostAndPort privateRpcAddress =
            HostAndPort.fromParts(
              common.getPrivateRpcAddressesList().get(0).getHost(),
              common.getPrivateRpcAddressesList().get(0).getPort());
          tserverInfo.setPrivateRpcAddress(privateRpcAddress);
        }

        if (!common.getHttpAddressesList().isEmpty()) {
          HostAndPort httpAddress =
            HostAndPort.fromParts(
              common.getHttpAddressesList().get(0).getHost(),
              common.getHttpAddressesList().get(0).getPort());
          tserverInfo.setHttpAddress(httpAddress);
        }

        tabletServers.add(tserverInfo);
      }
    }

    ListLiveTabletServersResponse response = new ListLiveTabletServersResponse(
        deadlineTracker.getElapsedMillis(), tsUUID, tabletServers,
        hasErr ? respBuilder.getError() : null);
    return new Pair<>(
        response, hasErr ? respBuilder.getError() : null);
  }

}
