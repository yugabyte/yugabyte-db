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
class GetFlagRequest extends YRpc<GetFlagResponse> {

  private String flag;

  public GetFlagRequest(String flag) {
    super(null);
    this.flag = flag;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final ServerBase.GetFlagRequestPB.Builder builder = ServerBase.GetFlagRequestPB.newBuilder();
    builder.setFlag(flag);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return GENERIC_SERVICE_NAME; }

  @Override
  String method() {
    return "GetFlag";
  }

  @Override
  Pair<GetFlagResponse, Object> deserialize(CallResponse callResponse,
                                         String uuid) throws Exception {
    final ServerBase.GetFlagResponsePB.Builder respBuilder =
        ServerBase.GetFlagResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    GetFlagResponse response = new GetFlagResponse(
        deadlineTracker.getElapsedMillis(), uuid, respBuilder.getValid(), respBuilder.getValue());
    return new Pair<GetFlagResponse, Object>(response, null);
  }
}
