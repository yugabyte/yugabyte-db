// Copyright (c) YugaByte, Inc.
package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.annotations.InterfaceAudience;
import org.yb.util.Pair;
import org.yb.tserver.TserverTypes;
import org.yb.tserver.TserverAdmin.UpgradeYsqlRequestPB;
import org.yb.tserver.TserverAdmin.UpgradeYsqlResponsePB;

@InterfaceAudience.Public
public class UpgradeYsqlRequest extends YRpc<UpgradeYsqlResponse> {

  private final boolean useSingleConnection;

  public UpgradeYsqlRequest(boolean useSingleConnection) {
    super(null);
    this.useSingleConnection = useSingleConnection;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final UpgradeYsqlRequestPB.Builder builder =
        UpgradeYsqlRequestPB.newBuilder()
            .setUseSingleConnection(useSingleConnection);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return TABLET_SERVER_ADMIN_SERVICE;
  }

  @Override
  String method() {
    return "UpgradeYsql";
  }

  @Override
  Pair<UpgradeYsqlResponse, Object> deserialize(CallResponse callResponse,
                                                String tsUUID) throws Exception {
    final UpgradeYsqlResponsePB.Builder respBuilder =
        UpgradeYsqlResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    final TserverTypes.TabletServerErrorPB error =
        respBuilder.hasError() ? respBuilder.getError() : null;

    UpgradeYsqlResponse response =
        new UpgradeYsqlResponse(deadlineTracker.getElapsedMillis(), tsUUID, error);

    return new Pair<>(response, error);
  }
}
