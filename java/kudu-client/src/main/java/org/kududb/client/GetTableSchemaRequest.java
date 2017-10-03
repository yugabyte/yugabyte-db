// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
package org.kududb.client;

import com.google.protobuf.Message;
import static org.kududb.master.Master.*;

import org.kududb.Schema;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.util.Pair;
import org.jboss.netty.buffer.ChannelBuffer;

/**
 * RPC to fetch a table's schema
 */
@InterfaceAudience.Private
public class GetTableSchemaRequest extends KuduRpc<GetTableSchemaResponse> {
  static final String GET_TABLE_SCHEMA = "GetTableSchema";
  private final String name;


  GetTableSchemaRequest(KuduTable masterTable, String name) {
    super(masterTable);
    this.name = name;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final GetTableSchemaRequestPB.Builder builder = GetTableSchemaRequestPB.newBuilder();
    TableIdentifierPB tableID =
        TableIdentifierPB.newBuilder().setTableName(name).build();
    builder.setTable(tableID);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return GET_TABLE_SCHEMA;
  }

  @Override
  Pair<GetTableSchemaResponse, Object> deserialize(CallResponse callResponse,
                                                   String tsUUID) throws Exception {
    final GetTableSchemaResponsePB.Builder respBuilder = GetTableSchemaResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    Schema schema = ProtobufHelper.pbToSchema(respBuilder.getSchema());
    GetTableSchemaResponse response = new GetTableSchemaResponse(
        deadlineTracker.getElapsedMillis(),
        tsUUID,
        schema,
        respBuilder.getTableId().toStringUtf8(),
        ProtobufHelper.pbToPartitionSchema(respBuilder.getPartitionSchema(), schema),
        respBuilder.getCreateTableDone());
    return new Pair<GetTableSchemaResponse, Object>(
        response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}
