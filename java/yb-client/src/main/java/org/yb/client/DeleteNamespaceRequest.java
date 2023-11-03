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
 * RPC to delete Namespace
 */
@InterfaceAudience.Private
class DeleteNamespaceRequest extends YRpc<DeleteNamespaceResponse> {

  static final String DELETE_NAMESPACE = "DeleteNamespace";

  private final String namespaceName;

  DeleteNamespaceRequest(YBTable masterTable, String namespaceName) {
    super(masterTable);
    this.namespaceName = namespaceName;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterDdlOuterClass.DeleteNamespaceRequestPB.Builder builder =
        MasterDdlOuterClass.DeleteNamespaceRequestPB.newBuilder();
    builder.setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder()
           .setName(this.namespaceName));
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return DELETE_NAMESPACE;
  }

  @Override
  Pair<DeleteNamespaceResponse, Object> deserialize(CallResponse callResponse,
                                                String masterUUID) throws Exception {
    final MasterDdlOuterClass.DeleteNamespaceResponsePB.Builder respBuilder =
        MasterDdlOuterClass.DeleteNamespaceResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    MasterTypes.MasterErrorPB err = respBuilder.getError();
    DeleteNamespaceResponse response = new DeleteNamespaceResponse(
        deadlineTracker.getElapsedMillis(), masterUUID, hasErr ? err : null);
    return new Pair<DeleteNamespaceResponse, Object>(response, hasErr ? err : null);
  }
}
