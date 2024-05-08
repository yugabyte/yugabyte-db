// Copyright (c) YugaByte, Inc.

package org.yb.client;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import java.util.Collection;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import org.yb.master.MasterAdminOuterClass;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;

public class AreNodesSafeToTakeDownRequest extends YRpc<AreNodesSafeToTakeDownResponse> {
  private final Collection<String> masters;
  private final Collection<String> tservers;
  private final Long followerLagBoundMs;

  AreNodesSafeToTakeDownRequest(
      YBTable table,
      Collection<String> masters,
      Collection<String> tservers,
      Long followerLagBoundMs) {
    super(table);
    this.tservers = tservers;
    this.masters = masters;
    this.followerLagBoundMs = followerLagBoundMs;
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "AreNodesSafeToTakeDown";
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    List<ByteString> tserverUuids = tservers.stream()
        .map(uuid -> ByteString.copyFromUtf8(uuid))
        .collect(Collectors.toList());
    List<ByteString> masterUuids = masters.stream()
        .map(uuid -> ByteString.copyFromUtf8(uuid))
        .collect(Collectors.toList());

    MasterAdminOuterClass.AreNodesSafeToTakeDownRequestPB request
        = MasterAdminOuterClass.AreNodesSafeToTakeDownRequestPB.newBuilder()
        .addAllMasterUuids(masterUuids)
        .addAllTserverUuids(tserverUuids)
        .setFollowerLagBoundMs(followerLagBoundMs)
        .build();
    return toChannelBuffer(header, request);
  }

  @Override
  Pair<AreNodesSafeToTakeDownResponse, Object> deserialize(CallResponse callResponse,
                                                           String tsUUID) throws Exception {
    final MasterAdminOuterClass.AreNodesSafeToTakeDownResponsePB.Builder respBuilder
        = MasterAdminOuterClass.AreNodesSafeToTakeDownResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    MasterTypes.MasterErrorPB error = respBuilder.hasError() ? respBuilder.getError() : null;

    AreNodesSafeToTakeDownResponse response = new AreNodesSafeToTakeDownResponse(error);
    return new Pair<>(response, error);
  }
}
