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
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterClusterOuterClass;
import org.yb.master.MasterClusterOuterClass.GetLoadBalancerStateRequestPB;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class GetLoadBalancerStateRequest extends YRpc<GetLoadBalancerStateResponse> {

  public static final Logger LOG = LoggerFactory.getLogger(GetLoadBalancerStateRequest.class);

  public GetLoadBalancerStateRequest(YBTable table) {
    // The passed table will be a master table from AsyncYBClient since this service is registered
    // on master.
    super(table);
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final GetLoadBalancerStateRequestPB.Builder builder =
        GetLoadBalancerStateRequestPB.newBuilder();
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "GetLoadBalancerState";
  }

  @Override
  Pair<GetLoadBalancerStateResponse, Object> deserialize(CallResponse callResponse, String uuid)
      throws Exception {
    final MasterClusterOuterClass.GetLoadBalancerStateResponsePB.Builder respBuilder =
        MasterClusterOuterClass.GetLoadBalancerStateResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);

    GetLoadBalancerStateResponse response =
        new GetLoadBalancerStateResponse(
            deadlineTracker.getElapsedMillis(), uuid, respBuilder.build());
    return new Pair<GetLoadBalancerStateResponse, Object>(
        response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}
