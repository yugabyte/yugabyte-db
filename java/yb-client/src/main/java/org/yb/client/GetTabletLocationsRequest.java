// Copyright (c) YugaByte, Inc.

package org.yb.client;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.stream.Collectors;
import org.yb.annotations.InterfaceAudience;
import org.yb.util.Pair;
import org.yb.master.MasterClientOuterClass;
import org.yb.master.MasterTypes;

@InterfaceAudience.Public
public class GetTabletLocationsRequest extends YRpc<GetTabletLocationsResponse> {
  private final List<ByteString> tabletIds;

  // Default value is null.
  private final ByteString tableId;

  // Default value is false.
  private final boolean includeInactive;

  // Default value is false.
  private final boolean includeDeleted;

  public GetTabletLocationsRequest(YBTable table,
                                  List<String> tabletIds,
                                  String tableId,
                                  boolean includeInactive,
                                  boolean includeDeleted) {
    super(table);
    this.tabletIds =
        tabletIds
            .stream()
            .map(tabletId -> ByteString.copyFrom(tabletId, StandardCharsets.UTF_8))
            .collect(Collectors.toList());
    if (tableId != null) {
      this.tableId = ByteString.copyFrom(tableId, StandardCharsets.UTF_8);
    } else {
      this.tableId = null;
    }
    this.includeInactive = includeInactive;
    this.includeDeleted = includeDeleted;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();

    final MasterClientOuterClass.GetTabletLocationsRequestPB.Builder builder =
        MasterClientOuterClass.GetTabletLocationsRequestPB.newBuilder()
            .addAllTabletIds(tabletIds)
            .setIncludeInactive(includeInactive)
            .setIncludeDeleted(includeDeleted);
    if (tableId != null) {
      builder.setTableId(tableId);
    }
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "GetTabletLocations";
  }

  @Override
  Pair<GetTabletLocationsResponse, Object> deserialize(CallResponse callResponse,
                                                      String tsUUID) throws Exception {
    final MasterClientOuterClass.GetTabletLocationsResponsePB.Builder builder =
        MasterClientOuterClass.GetTabletLocationsResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final MasterTypes.MasterErrorPB error = builder.hasError() ? builder.getError() : null;

    GetTabletLocationsResponse response =
        new GetTabletLocationsResponse(deadlineTracker.getElapsedMillis(),
                                      tsUUID,
                                      error,
                                      builder.getTabletLocationsList(),
                                      builder.getErrorsList(),
                                      builder.getPartitionListVersion());
    return new Pair<>(response, error);
  }
}
