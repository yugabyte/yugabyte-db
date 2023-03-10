// Copyright (c) YugaByte, Inc.
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

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;

import io.netty.buffer.ByteBuf;
import org.yb.cdc.CdcService;
import org.yb.cdc.CdcService.GetCheckpointForColocatedTableResponsePB;
import org.yb.cdc.CdcService.GetCheckpointForColocatedTableRequestPB;
import org.yb.util.Pair;

public class GetCheckpointForColocatedTableRequest
    extends YRpc<GetCheckpointForColocatedTableResponse> {
  private final String streamId;
  private final String tabletId;

  public String getTabletId() {
    return tabletId;
  }

  public GetCheckpointForColocatedTableRequest(YBTable table, String streamId, String tabletId) {
      super(table);
      this.streamId = streamId;
      this.tabletId = tabletId;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final GetCheckpointForColocatedTableRequestPB.Builder builder =
        GetCheckpointForColocatedTableRequestPB.newBuilder();
    builder.setStreamId(ByteString.copyFromUtf8(this.streamId));
    builder.setTabletId(ByteString.copyFromUtf8(this.tabletId));

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return CDC_SERVICE_NAME; }

  @Override
  String method() {
    return "GetCheckpointForColocatedTable";
  }

  @Override
  Pair<GetCheckpointForColocatedTableResponse, Object> deserialize(
          CallResponse callResponse, String uuid) throws Exception {
    final GetCheckpointForColocatedTableResponsePB.Builder respBuilder =
        GetCheckpointForColocatedTableResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    GetCheckpointForColocatedTableResponse response = new GetCheckpointForColocatedTableResponse(
            deadlineTracker.getElapsedMillis(), uuid, respBuilder.build());
    return new Pair<GetCheckpointForColocatedTableResponse, Object>(
        response, respBuilder.hasError() ? respBuilder.getError() : null);
  }

}
