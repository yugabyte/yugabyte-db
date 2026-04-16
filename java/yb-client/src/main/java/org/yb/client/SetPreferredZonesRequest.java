// Copyright (c) YugabyteDB, Inc.

package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import org.yb.CommonNet;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterClusterOuterClass;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class SetPreferredZonesRequest extends YRpc<SetPreferredZonesResponse> {

  private Map<Integer, List<CommonNet.CloudInfoPB>> prioritiesMap;

  public SetPreferredZonesRequest(YBTable table, Map<Integer,
      List<CommonNet.CloudInfoPB>> prioritiesMap) {
    super(table);
    this.prioritiesMap = prioritiesMap;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterClusterOuterClass.SetPreferredZonesRequestPB.Builder builder =
        MasterClusterOuterClass.SetPreferredZonesRequestPB.newBuilder();
    List<Integer> sortedPriorites = prioritiesMap.keySet().stream().sorted()
        .collect(Collectors.toList());
    for (Integer priority : sortedPriorites) {
      List<CommonNet.CloudInfoPB> placements = prioritiesMap.get(priority);
      CommonNet.CloudInfoListPB list = CommonNet.CloudInfoListPB.newBuilder()
          .addAllZones(placements)
          .build();
      builder.addMultiPreferredZones(list);
    }
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "SetPreferredZones";
  }

  @Override
  Pair<SetPreferredZonesResponse, Object> deserialize(
              CallResponse callResponse, String tsUUID) throws Exception {
    final MasterClusterOuterClass.SetPreferredZonesResponsePB.Builder respBuilder =
        MasterClusterOuterClass.SetPreferredZonesResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);

    SetPreferredZonesResponse response =
        new SetPreferredZonesResponse(deadlineTracker.getElapsedMillis(),
                                              tsUUID, respBuilder.getError());

    return new Pair<>(response, null);
  }

}
