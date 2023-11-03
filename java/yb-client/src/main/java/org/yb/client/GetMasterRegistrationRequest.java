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
import org.yb.CommonTypes.PeerRole;
import org.yb.annotations.InterfaceAudience;
import org.yb.util.Pair;

import static org.yb.master.MasterClusterOuterClass.GetMasterRegistrationRequestPB;
import static org.yb.master.MasterClusterOuterClass.GetMasterRegistrationResponsePB;
import static org.yb.master.MasterTypes.MasterErrorPB;
/**
 * Package-private RPC that can only go to master.
 */
@InterfaceAudience.Private
public class GetMasterRegistrationRequest extends YRpc<GetMasterRegistrationResponse> {
  private static final String GET_MASTER_REGISTRATION = "GetMasterRegistration";

  public GetMasterRegistrationRequest(YBTable masterTable) {
    super(masterTable);
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final GetMasterRegistrationRequestPB.Builder builder =
        GetMasterRegistrationRequestPB.newBuilder();
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return GET_MASTER_REGISTRATION;
  }

  @Override
  Pair<GetMasterRegistrationResponse, Object> deserialize(CallResponse callResponse,
                                                          String tsUUID) throws Exception {
    final GetMasterRegistrationResponsePB.Builder respBuilder =
        GetMasterRegistrationResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    PeerRole role = PeerRole.FOLLOWER;
    if (!respBuilder.hasError() || respBuilder.getError().getCode() !=
        MasterErrorPB.Code.CATALOG_MANAGER_NOT_INITIALIZED) {
      role = respBuilder.getRole();
    }
    GetMasterRegistrationResponse response = new GetMasterRegistrationResponse(
        deadlineTracker.getElapsedMillis(),
        tsUUID,
        role,
        respBuilder.getRegistration(),
        respBuilder.getInstanceId());
    return new Pair<GetMasterRegistrationResponse, Object>(
        response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}
