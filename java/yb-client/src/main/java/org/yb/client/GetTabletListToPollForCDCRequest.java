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

import org.yb.cdc.CdcService;
import org.yb.cdc.CdcService.TableInfo;
import org.yb.util.Pair;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;

public class GetTabletListToPollForCDCRequest extends YRpc<GetTabletListToPollForCDCResponse> {
  private final String streamId;
  private final String tableId;
  private final String tabletId;

  public GetTabletListToPollForCDCRequest(YBTable ybTable, String streamId, String tableId,
                                          String tabletId) {
    super(ybTable);
    this.streamId = streamId;
    this.tableId = tableId;
    this.tabletId = tabletId;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();

    final CdcService.GetTabletListToPollForCDCRequestPB.Builder builder = CdcService
      .GetTabletListToPollForCDCRequestPB.newBuilder();

    final TableInfo.Builder tableInfoBuilder = TableInfo.newBuilder();
    tableInfoBuilder.setStreamId(ByteString.copyFromUtf8(this.streamId));
    tableInfoBuilder.setTableId(ByteString.copyFromUtf8(this.tableId));

    builder.setTableInfo(tableInfoBuilder.build());

    builder.setTabletId(ByteString.copyFromUtf8(this.tabletId));

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return CDC_SERVICE_NAME;
  }

  @Override
  String method() {
    return "GetTabletListToPollForCDC";
  }

  public String getStreamId() {
    return this.streamId;
  }

  public String getTableId() {
    return this.tableId;
  }

  @Override
  Pair<GetTabletListToPollForCDCResponse, Object> deserialize(CallResponse callResponse,
                                                              String tsUUID) throws Exception {
    final CdcService.GetTabletListToPollForCDCResponsePB.Builder respBuilder = CdcService
      .GetTabletListToPollForCDCResponsePB.newBuilder();

    readProtobuf(callResponse.getPBMessage(), respBuilder);

    GetTabletListToPollForCDCResponse response = new GetTabletListToPollForCDCResponse(
        deadlineTracker.getElapsedMillis(),
        tsUUID,
        respBuilder.getTabletCheckpointPairsList());

    return new Pair<GetTabletListToPollForCDCResponse,Object>(
      response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}
