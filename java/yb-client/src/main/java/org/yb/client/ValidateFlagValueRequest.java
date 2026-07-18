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

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import org.yb.annotations.InterfaceAudience;
import org.yb.server.ServerBase;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class ValidateFlagValueRequest
    extends YRpc<ValidateFlagValueResponse> {

  private final String flagName;
  private final String flagValue;

  public ValidateFlagValueRequest(YBTable masterTable, String flagName, String flagValue) {
    super(masterTable);
    this.flagName = flagName;
    this.flagValue = flagValue;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final ServerBase.ValidateFlagValueRequestPB.Builder builder =
        ServerBase.ValidateFlagValueRequestPB.newBuilder();
    builder.setFlagName(flagName);
    builder.setFlagValue(flagValue);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return GENERIC_SERVICE_NAME;
  }

  @Override
  String method() {
    return "ValidateFlagValue";
  }

  @Override
  Pair<ValidateFlagValueResponse, Object> deserialize(
      CallResponse callResponse, String tsUUID) throws Exception {
    final ServerBase.ValidateFlagValueResponsePB.Builder respBuilder =
        ServerBase.ValidateFlagValueResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    ValidateFlagValueResponse response =
        new ValidateFlagValueResponse(
            deadlineTracker.getElapsedMillis(),
            tsUUID);
    return new Pair<>(response, null);
  }
}
