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
class IsLoadBalancedRequest extends YRpc<IsLoadBalancedResponse> {
  // Number of tservers which are expected to have their load balanced.
  int expectedServers = 0;

  public IsLoadBalancedRequest(YBTable masterTable, int numServers) {
    super(masterTable);
    expectedServers = numServers;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterClusterOuterClass.IsLoadBalancedRequestPB.Builder builder =
      MasterClusterOuterClass.IsLoadBalancedRequestPB.newBuilder();
    if (expectedServers != 0) {
      builder.setExpectedNumServers(expectedServers);
    }
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() { return "IsLoadBalanced"; }

  @Override
  Pair<IsLoadBalancedResponse, Object> deserialize(
      CallResponse callResponse,
      String masterUUID) throws Exception {
    final MasterClusterOuterClass.IsLoadBalancedResponsePB.Builder respBuilder =
      MasterClusterOuterClass.IsLoadBalancedResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    IsLoadBalancedResponse response =
      new IsLoadBalancedResponse(deadlineTracker.getElapsedMillis(),
                                 masterUUID,
                                 hasErr ? respBuilder.getErrorBuilder().build() : null);
    return new Pair<IsLoadBalancedResponse, Object>(response,
                                                    hasErr ? respBuilder.getError() : null);
  }
}
