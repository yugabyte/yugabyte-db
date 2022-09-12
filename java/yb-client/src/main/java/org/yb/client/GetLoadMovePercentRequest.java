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
import org.yb.util.Pair;

@InterfaceAudience.Public
class GetLoadMovePercentRequest extends YRpc<GetLoadMovePercentResponse> {
  public GetLoadMovePercentRequest(YBTable masterTable) {
    super(masterTable);
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterClusterOuterClass.GetLoadMovePercentRequestPB.Builder builder =
      MasterClusterOuterClass.GetLoadMovePercentRequestPB.newBuilder();

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() { return "GetLoadMoveCompletion"; }

  @Override
  Pair<GetLoadMovePercentResponse, Object> deserialize(
      CallResponse callResponse,
      String masterUUID) throws Exception {
    final MasterClusterOuterClass.GetLoadMovePercentResponsePB.Builder respBuilder =
      MasterClusterOuterClass.GetLoadMovePercentResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    GetLoadMovePercentResponse response =
      new GetLoadMovePercentResponse(
          deadlineTracker.getElapsedMillis(),
          masterUUID,
          hasErr ? 0 : respBuilder.getPercent(),
          hasErr ? 0 : respBuilder.getRemaining(),
          hasErr ? 0 : respBuilder.getTotal(),
          hasErr ? respBuilder.getErrorBuilder().build() : null);
    return new Pair<GetLoadMovePercentResponse, Object>(response,
                                                        hasErr ? respBuilder.getError() : null);
  }
}
