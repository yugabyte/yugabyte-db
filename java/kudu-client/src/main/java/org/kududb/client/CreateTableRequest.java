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
import org.kududb.Schema;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.master.Master;
import org.kududb.util.Pair;
import org.jboss.netty.buffer.ChannelBuffer;

/**
 * RPC to create new tables
 */
@InterfaceAudience.Private
class CreateTableRequest extends KuduRpc<CreateTableResponse> {

  static final String CREATE_TABLE = "CreateTable";

  private final Schema schema;
  private final String name;
  private final Master.CreateTableRequestPB.Builder builder;

  CreateTableRequest(KuduTable masterTable, String name, Schema schema,
                     CreateTableOptions builder) {
    super(masterTable);
    this.schema = schema;
    this.name = name;
    this.builder = builder.getBuilder();
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    this.builder.setName(this.name);
    this.builder.setSchema(ProtobufHelper.schemaToPb(this.schema));
    return toChannelBuffer(header, this.builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return CREATE_TABLE;
  }

  @Override
  Pair<CreateTableResponse, Object> deserialize(final CallResponse callResponse,
                                                String tsUUID) throws Exception {
    final Master.CreateTableResponsePB.Builder builder = Master.CreateTableResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), builder);
    CreateTableResponse response =
        new CreateTableResponse(deadlineTracker.getElapsedMillis(), tsUUID);
    return new Pair<CreateTableResponse, Object>(
        response, builder.hasError() ? builder.getError() : null);
  }
}
