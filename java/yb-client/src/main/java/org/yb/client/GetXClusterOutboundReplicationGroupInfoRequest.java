// Copyright (c) YugaByte, Inc.

package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import javax.annotation.Nonnull;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class GetXClusterOutboundReplicationGroupInfoRequest extends
    YRpc<GetXClusterOutboundReplicationGroupInfoResponse> {
  private final String replicationGroupName;

  GetXClusterOutboundReplicationGroupInfoRequest(
      YBTable table, @Nonnull String replicationGroupName) {
    super(table);
    this.replicationGroupName = replicationGroupName;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    return toChannelBuffer(header,
      MasterReplicationOuterClass.GetXClusterOutboundReplicationGroupInfoRequestPB.newBuilder()
        .setReplicationGroupId(replicationGroupName).build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "GetXClusterOutboundReplicationGroupInfo";
  }

  @Override
  Pair<GetXClusterOutboundReplicationGroupInfoResponse, Object> deserialize(
      CallResponse callResponse, String tsUUID) throws Exception {
    MasterReplicationOuterClass.GetXClusterOutboundReplicationGroupInfoResponsePB.Builder builder =
      MasterReplicationOuterClass.GetXClusterOutboundReplicationGroupInfoResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);
    MasterTypes.MasterErrorPB error = builder.hasError() ? builder.getError() : null;

    GetXClusterOutboundReplicationGroupInfoResponse response =
        new GetXClusterOutboundReplicationGroupInfoResponse(
            deadlineTracker.getElapsedMillis(), tsUUID, error, builder.getNamespaceInfosList());

    return new Pair<>(response, error);
  }
}
