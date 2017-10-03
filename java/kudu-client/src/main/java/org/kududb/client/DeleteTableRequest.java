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
import org.kududb.master.Master;
import org.kududb.util.Pair;
import org.jboss.netty.buffer.ChannelBuffer;

/**
 * RPC to delete tables
 */
@InterfaceAudience.Private
class DeleteTableRequest extends KuduRpc<DeleteTableResponse> {

  static final String DELETE_TABLE = "DeleteTable";

  private final String name;

  DeleteTableRequest(KuduTable table, String name) {
    super(table);
    this.name = name;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final Master.DeleteTableRequestPB.Builder builder = Master.DeleteTableRequestPB.newBuilder();
    Master.TableIdentifierPB tableID =
       Master.TableIdentifierPB.newBuilder().setTableName(name).build();
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
    final Master.DeleteTableResponsePB.Builder builder = Master.DeleteTableResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), builder);
    DeleteTableResponse response =
        new DeleteTableResponse(deadlineTracker.getElapsedMillis(), tsUUID);
    return new Pair<DeleteTableResponse, Object>(
        response, builder.hasError() ? builder.getError() : null);
  }
}
