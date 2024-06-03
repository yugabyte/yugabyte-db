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
import org.yb.master.MasterClusterOuterClass;
import org.yb.master.MasterClusterOuterClass.GetAutoFlagsConfigRequestPB;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class GetAutoFlagsConfigRequest extends YRpc<GetAutoFlagsConfigResponse> {

  public GetAutoFlagsConfigRequest(YBTable table) {
    // The passed table will be a master table from AsyncYBClient since this service is registered
    // on master.
    super(table);
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final GetAutoFlagsConfigRequestPB.Builder builder = GetAutoFlagsConfigRequestPB.newBuilder();
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "GetAutoFlagsConfig";
  }

  @Override
  Pair<GetAutoFlagsConfigResponse, Object> deserialize(
      CallResponse callResponse, String uuid) throws Exception {
    final MasterClusterOuterClass.GetAutoFlagsConfigResponsePB.Builder respBuilder =
      MasterClusterOuterClass.GetAutoFlagsConfigResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    GetAutoFlagsConfigResponse response = new GetAutoFlagsConfigResponse(
        deadlineTracker.getElapsedMillis(), uuid, respBuilder.build());
    return new Pair<GetAutoFlagsConfigResponse, Object>(
        response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}
