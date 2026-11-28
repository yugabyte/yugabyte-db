// Copyright (c) YugabyteDB, Inc.
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

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterClusterOuterClass;
import org.yb.util.Pair;

@InterfaceAudience.Public
class RemoveTabletServerRequest extends YRpc<RemoveTabletServerResponse> {
  private final String permanentUuid;

  public RemoveTabletServerRequest(YBTable masterTable, String permanentUuid) {
    super(masterTable);
    this.permanentUuid = permanentUuid;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterClusterOuterClass.RemoveTabletServerRequestPB.Builder builder =
        MasterClusterOuterClass.RemoveTabletServerRequestPB.newBuilder();
    builder.setPermanentUuid(ByteString.copyFromUtf8(permanentUuid));
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() { return "RemoveTabletServer"; }

  @Override
  Pair<RemoveTabletServerResponse, Object> deserialize(
      CallResponse callResponse,
      String masterUUID) throws Exception {
    final MasterClusterOuterClass.RemoveTabletServerResponsePB.Builder respBuilder =
        MasterClusterOuterClass.RemoveTabletServerResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    RemoveTabletServerResponse response =
        new RemoveTabletServerResponse(deadlineTracker.getElapsedMillis(), masterUUID);
    return new Pair<RemoveTabletServerResponse, Object>(
        response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}
