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

import org.yb.master.MasterAdminOuterClass.SplitTabletRequestPB;
import org.yb.master.MasterAdminOuterClass.SplitTabletResponsePB;
import org.yb.util.Pair;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;

import io.netty.buffer.ByteBuf;

public class SplitTabletRequest extends YRpc<SplitTabletResponse> {
  private final String tabletId;

  public SplitTabletRequest(YBTable table, String tabletId) {
    super(table);
    this.tabletId = tabletId;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final SplitTabletRequestPB.Builder builder = SplitTabletRequestPB.newBuilder();
    builder.setTabletId(ByteString.copyFromUtf8(this.tabletId));
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "SplitTablet";
  }

  @Override
  Pair<SplitTabletResponse, Object> deserialize(CallResponse callResponse,
                                                String tsUUID) throws Exception {
    final SplitTabletResponsePB.Builder respBuilder = SplitTabletResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);

    SplitTabletResponse response =
      new SplitTabletResponse(deadlineTracker.getElapsedMillis(), tsUUID);
    return new Pair<SplitTabletResponse,Object>(
      response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}
