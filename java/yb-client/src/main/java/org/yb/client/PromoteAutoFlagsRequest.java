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
public class PromoteAutoFlagsRequest extends YRpc<PromoteAutoFlagsResponse> {
  private String maxFlagClass;
  private boolean promoteNonRuntimeFlags;
  private boolean force;

  public PromoteAutoFlagsRequest(YBTable table, String maxFlagClass, boolean promoteNonRuntimeFlags,
                                 boolean force) {
    // This table would be the master table from AsyncYBClient since this service is registered
    // on master.
    super(table);
    this.maxFlagClass = maxFlagClass;
    this.promoteNonRuntimeFlags = promoteNonRuntimeFlags;
    this.force = force;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert  header.isInitialized();
    final MasterClusterOuterClass.PromoteAutoFlagsRequestPB.Builder builder =
      MasterClusterOuterClass.PromoteAutoFlagsRequestPB.newBuilder();
    builder.setMaxFlagClass(this.maxFlagClass);
    builder.setPromoteNonRuntimeFlags(this.promoteNonRuntimeFlags);
    builder.setForce(this.force);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "PromoteAutoFlags";
  }

  @Override
  Pair<PromoteAutoFlagsResponse, Object> deserialize(
    CallResponse callResponse, String uuid) throws Exception {
    final MasterClusterOuterClass.PromoteAutoFlagsResponsePB.Builder respBuilder =
      MasterClusterOuterClass.PromoteAutoFlagsResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    PromoteAutoFlagsResponse response = new PromoteAutoFlagsResponse(
        deadlineTracker.getElapsedMillis(), uuid, respBuilder.build());
    return new Pair<PromoteAutoFlagsResponse, Object>(
        response, null);
  }
}
