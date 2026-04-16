package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterReplicationOuterClass.IsBootstrapRequiredResponsePB.TableResult;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.util.Pair;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class IsBootstrapRequiredRequest extends YRpc<IsBootstrapRequiredResponse> {

  // A map of table ids to their corresponding stream id if any.
  private final Map<String, String> tableIdsStreamIdMap;

  IsBootstrapRequiredRequest(YBTable table, Map<String, String> tableIdsStreamIdMap) {
    super(table);
    this.tableIdsStreamIdMap = tableIdsStreamIdMap;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();

    List<String> tableIds = new ArrayList<>();
    List<String> streamIds = new ArrayList<>();
    tableIdsStreamIdMap.forEach((tableId, streamId) -> {
      tableIds.add(tableId);
      if (streamId == null) {
        streamIds.add("");
      } else {
        streamIds.add(streamId);
      }
    });
    final MasterReplicationOuterClass.IsBootstrapRequiredRequestPB.Builder builder =
        MasterReplicationOuterClass.IsBootstrapRequiredRequestPB.newBuilder();
    builder.addAllTableIds(tableIds);
    builder.addAllStreamIds(streamIds);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "IsBootstrapRequired";
  }

  @Override
  Pair<IsBootstrapRequiredResponse, Object> deserialize(
    CallResponse callResponse, String tsUUID) throws Exception {
    final MasterReplicationOuterClass.IsBootstrapRequiredResponsePB.Builder builder =
        MasterReplicationOuterClass.IsBootstrapRequiredResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), builder);

    final MasterErrorPB error = builder.hasError() ? builder.getError() : null;
    final Map<String, Boolean> results = new HashMap<>();
    for (TableResult tableResult: builder.getResultsList()) {
      // Result must contain table id and whether it needs bootstrap. It is optional in the
      // protobuf definition, but table id must exist for this purpose.
      if (!tableResult.hasTableId() || !tableResult.hasBootstrapRequired()) {
        throw new RuntimeException("tableId or bootstrapRequired is not present in the response "
            + "from IsBootstrapRequired RPC call");
      }
      String tableId = tableResult.getTableId().toStringUtf8();
      if (results.containsKey(tableId)) {
        throw new RuntimeException("Duplicate tableIds detected in the response from "
            + "IsBootstrapRequired RPC call");
      }
      results.put(tableId, tableResult.getBootstrapRequired());
    }

    IsBootstrapRequiredResponse response =
        new IsBootstrapRequiredResponse(
            deadlineTracker.getElapsedMillis(), tsUUID, error, results);

    return new Pair<>(response, error);
  }
}
