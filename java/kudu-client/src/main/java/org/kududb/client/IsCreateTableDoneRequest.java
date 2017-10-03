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

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.master.Master;
import org.kududb.util.Pair;
import org.jboss.netty.buffer.ChannelBuffer;

/**
 * Package-private RPC that can only go to a master.
 */
@InterfaceAudience.Private
class IsCreateTableDoneRequest extends KuduRpc<Master.IsCreateTableDoneResponsePB> {

  private final String tableId;

  IsCreateTableDoneRequest(KuduTable table, String tableId) {
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
  Pair<Master.IsCreateTableDoneResponsePB, Object> deserialize(
      final CallResponse callResponse, String tsUUID) throws Exception {
    Master.IsCreateTableDoneResponsePB.Builder builder = Master.IsCreateTableDoneResponsePB
        .newBuilder();
    readProtobuf(callResponse.getPBMessage(), builder);
    Master.IsCreateTableDoneResponsePB resp = builder.build();
    return new Pair<Master.IsCreateTableDoneResponsePB, Object>(
        resp, builder.hasError() ? builder.getError() : null);
  }

  @Override
  ChannelBuffer serialize(Message header) {
    final Master.IsCreateTableDoneRequestPB.Builder builder = Master
        .IsCreateTableDoneRequestPB.newBuilder();
    builder.setTable(Master.TableIdentifierPB.newBuilder().setTableId(
        ByteString.copyFromUtf8(tableId)));
    return toChannelBuffer(header, builder.build());
  }
}
