// Copyright (c) YugaByte, Inc.

package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import java.util.List;
import java.util.stream.Collectors;
import org.yb.annotations.InterfaceAudience;
import org.yb.tserver.TserverService.ListTabletsForTabletServerRequestPB;
import org.yb.tserver.TserverService.ListTabletsForTabletServerResponsePB;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class ListTabletsForTabletServerRequest extends YRpc<ListTabletsForTabletServerResponse> {

  public ListTabletsForTabletServerRequest() {
    super(null);
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final ListTabletsForTabletServerRequestPB.Builder builder =
        ListTabletsForTabletServerRequestPB.newBuilder();
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return TABLET_SERVER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "ListTabletsForTabletServer";
  }

  @Override
  Pair<ListTabletsForTabletServerResponse, Object> deserialize(
              CallResponse callResponse, String tsUUID) throws Exception {
    final ListTabletsForTabletServerResponsePB.Builder respBuilder =
        ListTabletsForTabletServerResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);

    List<String> tabletIds = respBuilder.getEntriesList()
                                        .stream()
                                        .map(entry -> entry.getTabletId().toStringUtf8())
                                        .collect(Collectors.toList());

    ListTabletsForTabletServerResponse response =
        new ListTabletsForTabletServerResponse(deadlineTracker.getElapsedMillis(),
                                              tsUUID, tabletIds);

    return new Pair<ListTabletsForTabletServerResponse, Object>(response, null);
  }

}
