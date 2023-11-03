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
import org.yb.server.ServerBase;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class GetStatusRequest extends YRpc<GetStatusResponse> {

  public GetStatusRequest() {
    super(null);
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final ServerBase.GetStatusRequestPB.Builder builder =
      ServerBase.GetStatusRequestPB.newBuilder();
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return GENERIC_SERVICE_NAME;
  }

  @Override
  String method() {
    return "GetStatus";
  }

  @Override
  Pair<GetStatusResponse, Object> deserialize(CallResponse callResponse, String uuid)
    throws Exception {
    final ServerBase.GetStatusResponsePB.Builder respBuilder =
      ServerBase.GetStatusResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    GetStatusResponse response =
      new GetStatusResponse(deadlineTracker.getElapsedMillis(), uuid, respBuilder.build());
    return new Pair<GetStatusResponse, Object>(response, null);
  }
}
