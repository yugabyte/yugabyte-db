// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.client;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;

import org.yb.tserver.TserverAdmin.FlushTabletsRequestPB;
import org.yb.tserver.TserverAdmin.FlushTabletsRequestPB.Operation;
import org.yb.tserver.TserverAdmin.FlushTabletsResponsePB;
import org.yb.util.CommonUtil;
import org.yb.util.Pair;

public class FlushTabletsRequest extends YRpc<FlushTabletsResponse> {
  private final String permanentUuid;
  private final List<String> tabletIds;

  FlushTabletsRequest(YBTable table, String permanentUuid, List<String> tabletIds) {
    super(table);
    this.permanentUuid = permanentUuid;
    this.tabletIds = tabletIds;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final FlushTabletsRequestPB.Builder builder = FlushTabletsRequestPB.newBuilder();
    if (tabletIds == null || tabletIds.isEmpty()) {
      builder.setAllTablets(true);
    } else {
      builder.addAllTabletIds(
          tabletIds.stream().map(ByteString::copyFromUtf8).collect(Collectors.toList()));
    }
    builder.setDestUuid(ByteString.copyFromUtf8(permanentUuid));
    builder.setOperation(Operation.FLUSH);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return TABLET_SERVER_ADMIN_SERVICE;
  }

  @Override
  String method() {
    return "FlushTablets";
  }

  @Override
  Pair<FlushTabletsResponse, Object> deserialize(CallResponse callResponse, String tsUUID)
      throws Exception {
    final FlushTabletsResponsePB.Builder builder = FlushTabletsResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), builder);
    final FlushTabletsResponse response =
        new FlushTabletsResponse(deadlineTracker.getElapsedMillis(), tsUUID);
    return new Pair<FlushTabletsResponse, Object>(response,
        builder.hasError() ? builder.getError() : null);
  }
}
