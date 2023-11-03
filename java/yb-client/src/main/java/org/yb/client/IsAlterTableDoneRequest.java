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

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;

import static org.yb.master.MasterDdlOuterClass.IsAlterTableDoneRequestPB;
import static org.yb.master.MasterDdlOuterClass.IsAlterTableDoneResponsePB;
import static org.yb.master.MasterTypes.TableIdentifierPB;

/**
 * RPC used to check if an alter is running for the specified table
 */
@InterfaceAudience.Private
class IsAlterTableDoneRequest extends YRpc<IsAlterTableDoneResponse> {

  static final String IS_ALTER_TABLE_DONE = "IsAlterTableDone";
  private final String name;
  private final String keyspace;


  IsAlterTableDoneRequest(YBTable masterTable, String name, String keyspace) {
    super(masterTable);
    this.name = name;
    this.keyspace = keyspace;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final IsAlterTableDoneRequestPB.Builder builder = IsAlterTableDoneRequestPB.newBuilder();
    TableIdentifierPB tableID = TableIdentifierPB.newBuilder()
                                .setTableName(name)
                                .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder()
                                              .setName(this.keyspace))
                                .build();
    builder.setTable(tableID);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return IS_ALTER_TABLE_DONE;
  }

  @Override
  Pair<IsAlterTableDoneResponse, Object> deserialize(final CallResponse callResponse,
                                                       String tsUUID) throws Exception {
    final IsAlterTableDoneResponsePB.Builder respBuilder = IsAlterTableDoneResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    IsAlterTableDoneResponse resp = new IsAlterTableDoneResponse(deadlineTracker.getElapsedMillis(),
        tsUUID, respBuilder.getDone());
    return new Pair<IsAlterTableDoneResponse, Object>(
        resp, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}
