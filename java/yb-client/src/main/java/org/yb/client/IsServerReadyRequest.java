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
import org.yb.annotations.InterfaceAudience;
import org.yb.tserver.Tserver;
import org.yb.util.Pair;

@InterfaceAudience.Public
class IsServerReadyRequest extends YRpc<IsServerReadyResponse> {
  public IsServerReadyRequest() {
    super(null);
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final Tserver.IsTabletServerReadyRequestPB.Builder builder =
        Tserver.IsTabletServerReadyRequestPB.newBuilder();
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return TABLET_SERVER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "IsTabletServerReady";
  }

  @Override
  Pair<IsServerReadyResponse, Object> deserialize(
      CallResponse callResponse, String uuid) throws Exception {
    final Tserver.IsTabletServerReadyResponsePB.Builder respBuilder =
        Tserver.IsTabletServerReadyResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasError = respBuilder.hasError();
    IsServerReadyResponse response =
        new IsServerReadyResponse(deadlineTracker.getElapsedMillis(), uuid,
                                  hasError ? respBuilder.getErrorBuilder().build() : null,
                                  respBuilder.getNumTabletsNotRunning(),
                                  respBuilder.getTotalTablets());
    return new Pair<IsServerReadyResponse, Object>(response,
                                                   hasError ? respBuilder.getError() : null);
  }
}
