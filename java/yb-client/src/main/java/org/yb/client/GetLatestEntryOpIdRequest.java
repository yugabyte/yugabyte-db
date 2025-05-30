// Copyright (c) YugaByte, Inc.

package org.yb.client;

import java.util.List;
import java.util.stream.Collectors;

import org.yb.annotations.InterfaceAudience;
import org.yb.cdc.CdcService.GetLatestEntryOpIdRequestPB;
import org.yb.cdc.CdcService.GetLatestEntryOpIdResponsePB;
import org.yb.util.Pair;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;

import io.netty.buffer.ByteBuf;

@InterfaceAudience.Public
public class GetLatestEntryOpIdRequest extends YRpc<GetLatestEntryOpIdResponse> {

  private final List<String> tablet_ids;

  public GetLatestEntryOpIdRequest(List<String> tablet_ids) {
    super(null);
    this.tablet_ids = tablet_ids;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final GetLatestEntryOpIdRequestPB.Builder builder = GetLatestEntryOpIdRequestPB.newBuilder();
    builder.addAllTabletIds(tablet_ids.stream()
        .map(id -> ByteString.copyFromUtf8(id)).collect(Collectors.toList()));
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return CDC_SERVICE_NAME;
  }

  @Override
  String method() {
    return "GetLatestEntryOpId";
  }

  @Override
  Pair<GetLatestEntryOpIdResponse, Object> deserialize(
      CallResponse callResponse, String tsUUID) throws Exception {
    final GetLatestEntryOpIdResponsePB.Builder builder = GetLatestEntryOpIdResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), builder);
    GetLatestEntryOpIdResponse response = new GetLatestEntryOpIdResponse(
        deadlineTracker.getElapsedMillis(), tsUUID,
        builder.getOpIdsList(), builder.getError(), builder.getBootstrapTime());
    return new Pair(response, null);
  }

}
