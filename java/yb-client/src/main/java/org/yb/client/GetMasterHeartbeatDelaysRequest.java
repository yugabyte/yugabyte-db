// Copyright (c) YugaByte, Inc.
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
import java.util.UUID;
import java.util.HashMap;
import java.util.Map;
import org.yb.annotations.InterfaceAudience;
import org.yb.util.Pair;
import org.yb.master.MasterAdminOuterClass.GetMasterHeartbeatDelaysResponsePB;
import org.yb.master.MasterAdminOuterClass.GetMasterHeartbeatDelaysRequestPB;
import org.yb.master.MasterAdminOuterClass.GetMasterHeartbeatDelaysResponsePB.MasterHeartbeatDelay;

@InterfaceAudience.Public
class GetMasterHeartbeatDelaysRequest extends YRpc<GetMasterHeartbeatDelaysResponse> {

  GetMasterHeartbeatDelaysRequest(YBTable ybTable) {
    super(ybTable);
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "GetMasterHeartbeatDelays";
  }

  @Override
  Pair<GetMasterHeartbeatDelaysResponse, Object> deserialize(
      CallResponse callResponse, String uuid) throws Exception {
    final GetMasterHeartbeatDelaysResponsePB.Builder respBuilder =
        GetMasterHeartbeatDelaysResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    Map<String, Long> masterHeartbeatDelays = new HashMap<>();
    if (!hasErr) {
      for (MasterHeartbeatDelay delay : respBuilder.getHeartbeatDelayList()) {
        if (!delay.hasMasterUuid() || !delay.hasLastHeartbeatDeltaMs()) {
          throw new RuntimeException(
            "Master UUID or heartbeat details missing in heartbeat delay response");
        }
        String masterUUID = delay.getMasterUuid().toStringUtf8();
        masterHeartbeatDelays.put(masterUUID, delay.getLastHeartbeatDeltaMs());
      }
    }
    GetMasterHeartbeatDelaysResponse response =
        new GetMasterHeartbeatDelaysResponse(
            deadlineTracker.getElapsedMillis(),
            uuid,
            hasErr ? respBuilder.getErrorBuilder().build() : null,
            masterHeartbeatDelays);
    return new Pair<GetMasterHeartbeatDelaysResponse, Object>(
      response, hasErr ? respBuilder.getError() : null);
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final GetMasterHeartbeatDelaysRequestPB.Builder reqBuilder =
        GetMasterHeartbeatDelaysRequestPB.newBuilder();
    return toChannelBuffer(header, reqBuilder.build());
  }
}
