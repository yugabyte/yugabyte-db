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
import org.yb.annotations.InterfaceAudience;
import org.yb.util.Pair;
import io.netty.buffer.ByteBuf;
import org.yb.server.ServerBase;

@InterfaceAudience.Public
class SetFlagRequest extends YRpc<SetFlagResponse> {

  private String flag;
  private String value;
  private boolean force = false;

  public SetFlagRequest(String flag, String value) {
    super(null);
    this.flag = flag;
    this.value = value;
  }

  public SetFlagRequest(String flag, String value, boolean force) {
    super(null);
    this.flag = flag;
    this.value = value;
    this.force = force;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final ServerBase.SetFlagRequestPB.Builder builder = ServerBase.SetFlagRequestPB.newBuilder();
    builder.setFlag(flag);
    builder.setValue(value);
    builder.setForce(force);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return GENERIC_SERVICE_NAME; }

  @Override
  String method() {
    return "SetFlag";
  }

  @Override
  Pair<SetFlagResponse, Object> deserialize(CallResponse callResponse,
                                         String uuid) throws Exception {
    final ServerBase.SetFlagResponsePB.Builder respBuilder =
        ServerBase.SetFlagResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    SetFlagResponse response =
        new SetFlagResponse(deadlineTracker.getElapsedMillis(), uuid, respBuilder.getResult());
    return new Pair<SetFlagResponse, Object>(response, null);
  }
}
