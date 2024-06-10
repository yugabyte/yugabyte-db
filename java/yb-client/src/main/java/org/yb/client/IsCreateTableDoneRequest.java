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
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;
import io.netty.buffer.ByteBuf;

/**
 * Package-private RPC that can only go to a master.
 */
@InterfaceAudience.Private
class IsCreateTableDoneRequest extends YRpc<MasterDdlOuterClass.IsCreateTableDoneResponsePB> {

  private final String tableId;

  IsCreateTableDoneRequest(YBTable table, String tableId) {
    super(table);
    this.tableId = tableId;
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "IsCreateTableDone";
  }

  @Override
  Pair<MasterDdlOuterClass.IsCreateTableDoneResponsePB, Object> deserialize(
      final CallResponse callResponse, String tsUUID) throws Exception {
    MasterDdlOuterClass.IsCreateTableDoneResponsePB.Builder builder =
        MasterDdlOuterClass.IsCreateTableDoneResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), builder);
    MasterDdlOuterClass.IsCreateTableDoneResponsePB resp = builder.build();
    return new Pair<MasterDdlOuterClass.IsCreateTableDoneResponsePB, Object>(
        resp, builder.hasError() ? builder.getError() : null);
  }

  @Override
  ByteBuf serialize(Message header) {
    final MasterDdlOuterClass.IsCreateTableDoneRequestPB.Builder builder = MasterDdlOuterClass
        .IsCreateTableDoneRequestPB.newBuilder();
    builder.setTable(MasterTypes.TableIdentifierPB.newBuilder().setTableId(
        ByteString.copyFromUtf8(tableId)));
    return toChannelBuffer(header, builder.build());
  }
}
