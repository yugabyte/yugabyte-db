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
import org.yb.consensus.Metadata;
import org.yb.WireProtocol;
import org.yb.annotations.InterfaceAudience;
import org.yb.util.Pair;
import io.netty.buffer.ByteBuf;
import org.yb.server.ServerBase;

@InterfaceAudience.Public
class PingRequest extends YRpc<PingResponse> {
  public PingRequest() {
    super(null);
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final ServerBase.PingRequestPB.Builder builder =
      ServerBase.PingRequestPB.newBuilder();
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return GENERIC_SERVICE_NAME; }

  @Override
  String method() {
    return "Ping";
  }

  @Override
  Pair<PingResponse, Object> deserialize(CallResponse callResponse,
                                         String uuid) throws Exception {
    final ServerBase.PingResponsePB.Builder respBuilder = ServerBase.PingResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    PingResponse response = new PingResponse(deadlineTracker.getElapsedMillis(), uuid);

    return new Pair<PingResponse, Object>(response, null);
  }
}
