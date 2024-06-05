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
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;

/**
 * RPC to delete tables
 */
@InterfaceAudience.Private
class DeleteTableRequest extends YRpc<DeleteTableResponse> {

  static final String DELETE_TABLE = "DeleteTable";

  private final String name;
  private final String keyspace;

  DeleteTableRequest(YBTable table, String name, String keyspace) {
    super(table);
    this.name = name;
    this.keyspace = keyspace;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterDdlOuterClass.DeleteTableRequestPB.Builder builder =
        MasterDdlOuterClass.DeleteTableRequestPB.newBuilder();
    MasterTypes.TableIdentifierPB.Builder tbuilder = MasterTypes.TableIdentifierPB.newBuilder();
    MasterTypes.TableIdentifierPB tableID;
    tbuilder.setTableName(name);
    tableID = tbuilder
              .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName(this.keyspace))
              .build();
    builder.setTable(tableID);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return DELETE_TABLE;
  }

  @Override
  Pair<DeleteTableResponse, Object> deserialize(CallResponse callResponse,
                                                String tsUUID) throws Exception {
    final MasterDdlOuterClass.DeleteTableResponsePB.Builder builder =
        MasterDdlOuterClass.DeleteTableResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), builder);
    DeleteTableResponse response =
        new DeleteTableResponse(deadlineTracker.getElapsedMillis(), tsUUID);
    return new Pair<DeleteTableResponse, Object>(
        response, builder.hasError() ? builder.getError() : null);
  }
}
