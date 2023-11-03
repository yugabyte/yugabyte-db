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

import org.yb.master.MasterAdminOuterClass.FlushTablesRequestPB;
import org.yb.master.MasterAdminOuterClass.FlushTablesResponsePB;
import org.yb.master.MasterTypes.TableIdentifierPB;
import org.yb.util.Pair;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;

import io.netty.buffer.ByteBuf;

public class FlushTableRequest extends YRpc<FlushTableResponse> {
  private final String tableId;

  public FlushTableRequest(YBTable table, String tableId) {
    super(table);
    this.tableId = tableId;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    TableIdentifierPB.Builder tableIdentifierBuilder = TableIdentifierPB.newBuilder();
    tableIdentifierBuilder.setTableId(ByteString.copyFromUtf8(this.tableId));

    final FlushTablesRequestPB.Builder builder = FlushTablesRequestPB.newBuilder();
    builder.addTables(tableIdentifierBuilder);
    // We want to compact the tables while flushing them.
    builder.setIsCompaction(true);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "FlushTables";
  }

  @Override
  Pair<FlushTableResponse, Object> deserialize(CallResponse callResponse,
                                               String tsUUID) throws Exception {
    final FlushTablesResponsePB.Builder respBuilder = FlushTablesResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);

    FlushTableResponse response = new FlushTableResponse(
      deadlineTracker.getElapsedMillis(), tsUUID, respBuilder.getFlushRequestId().toStringUtf8());
    return new Pair<FlushTableResponse,Object>(
      response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}
