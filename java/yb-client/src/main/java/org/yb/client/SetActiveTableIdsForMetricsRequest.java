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

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import java.util.Collection;
import org.yb.annotations.InterfaceAudience;
import org.yb.tserver.TserverService.SetActiveTableIdsForMetricsRequestPB;
import org.yb.tserver.TserverService.SetActiveTableIdsForMetricsResponsePB;
import org.yb.util.Pair;

@InterfaceAudience.Public
class SetActiveTableIdsForMetricsRequest extends YRpc<SetActiveTableIdsForMetricsResponse> {

  private final Collection<String> tableIds;
  private final int listVersion;

  public SetActiveTableIdsForMetricsRequest(Collection<String> tableIds, int listVersion) {
    super(null);
    this.tableIds = tableIds;
    this.listVersion = listVersion;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final SetActiveTableIdsForMetricsRequestPB.Builder builder =
        SetActiveTableIdsForMetricsRequestPB.newBuilder()
            .addAllTableIds(tableIds)
            .setListVersion(listVersion);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return TABLET_SERVER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "SetActiveTableIdsForMetrics";
  }

  @Override
  Pair<SetActiveTableIdsForMetricsResponse, Object> deserialize(
      CallResponse callResponse, String uuid) throws Exception {
    final SetActiveTableIdsForMetricsResponsePB.Builder respBuilder =
        SetActiveTableIdsForMetricsResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    // A rejected list (for example one above the TServer's table count limit) is reported on the
    // response rather than thrown, so the caller can tell it apart from an RPC failure.
    SetActiveTableIdsForMetricsResponse response =
        new SetActiveTableIdsForMetricsResponse(
            deadlineTracker.getElapsedMillis(),
            uuid,
            respBuilder.hasError() ? respBuilder.getError() : null,
            respBuilder.getFilteringEnabled());
    return new Pair<>(response, null);
  }
}
