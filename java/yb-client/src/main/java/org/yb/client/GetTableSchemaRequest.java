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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
import org.yb.IndexInfo;
import org.yb.Schema;
import org.yb.annotations.InterfaceAudience;
import org.yb.util.Pair;

import java.util.List;

import static org.yb.master.MasterDdlOuterClass.GetTableSchemaRequestPB;
import static org.yb.master.MasterDdlOuterClass.GetTableSchemaResponsePB;
import static org.yb.master.MasterTypes.NamespaceIdentifierPB;
import static org.yb.master.MasterTypes.TableIdentifierPB;

/**
 * RPC to fetch a table's schema
 */
@InterfaceAudience.Private
public class GetTableSchemaRequest extends YRpc<GetTableSchemaResponse> {
  static final String GET_TABLE_SCHEMA = "GetTableSchema";
  private final String name;
  private final String uuid;
  private final String keyspace;

  GetTableSchemaRequest(YBTable masterTable, String name, String uuid) {
    this(masterTable, name, uuid, null);
  }

  GetTableSchemaRequest(YBTable masterTable, String name, String uuid, String keyspace) {
    super(masterTable);
    this.name = name;
    this.uuid = uuid;
    this.keyspace = keyspace;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    assert name != null || uuid != null;
    final GetTableSchemaRequestPB.Builder builder = GetTableSchemaRequestPB.newBuilder();
    TableIdentifierPB tableID;
    TableIdentifierPB.Builder tbuilder = TableIdentifierPB.newBuilder();
    if (name == null) {
      tbuilder.setTableId(ByteString.copyFrom(Bytes.fromString(uuid)));
    } else {
      tbuilder.setTableName(name);
      tbuilder.setNamespace(NamespaceIdentifierPB.newBuilder().setName(this.keyspace));
    }
    tableID = tbuilder.build();

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
    List<IndexInfo> indexes = ProtobufHelper.pbToIndexes(respBuilder.getIndexesList());
    GetTableSchemaResponse response = new GetTableSchemaResponse(
        deadlineTracker.getElapsedMillis(),
        tsUUID,
        schema,
        respBuilder.getIdentifier().getNamespace().getName(),
        respBuilder.getIdentifier().getTableName(),
        respBuilder.getIdentifier().getTableId().toStringUtf8(),
        ProtobufHelper.pbToPartitionSchema(respBuilder.getPartitionSchema(), schema),
        respBuilder.getCreateTableDone(),
        respBuilder.getTableType(),
        indexes,
        respBuilder.getColocated());
    return new Pair<GetTableSchemaResponse, Object>(
        response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}
