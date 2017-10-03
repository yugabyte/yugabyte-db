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
import org.kududb.annotations.InterfaceAudience;
import org.kududb.util.Pair;
import org.jboss.netty.buffer.ChannelBuffer;

import static org.kududb.master.Master.*;

/**
 * RPC used to alter a table. When it returns it doesn't mean that the table is altered,
 * a success just means that the master accepted it.
 */
@InterfaceAudience.Private
class AlterTableRequest extends KuduRpc<AlterTableResponse> {

  static final String ALTER_TABLE = "AlterTable";
  private final String name;
  private final AlterTableRequestPB.Builder builder;

  AlterTableRequest(KuduTable masterTable, String name, AlterTableOptions ato) {
    super(masterTable);
    this.name = name;
    this.builder = ato.pb;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    TableIdentifierPB tableID =
        TableIdentifierPB.newBuilder().setTableName(name).build();
    this.builder.setTable(tableID);
    return toChannelBuffer(header, this.builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return ALTER_TABLE;
  }

  @Override
  Pair<AlterTableResponse, Object> deserialize(final CallResponse callResponse,
                                                String tsUUID) throws Exception {
    final AlterTableResponsePB.Builder respBuilder = AlterTableResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    AlterTableResponse response = new AlterTableResponse(deadlineTracker.getElapsedMillis(),
        tsUUID);
    return new Pair<AlterTableResponse, Object>(
        response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}
