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
import org.yb.util.Pair;

import org.yb.master.MasterEncryptionOuterClass;
import org.yb.master.MasterTypes;

public class IsEncryptionEnabledRequest extends YRpc<IsEncryptionEnabledResponse> {

  public IsEncryptionEnabledRequest(YBTable table) {
    super(table);
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterEncryptionOuterClass.IsEncryptionEnabledRequestPB.Builder builder =
            MasterEncryptionOuterClass.IsEncryptionEnabledRequestPB.newBuilder();
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "IsEncryptionEnabled";
  }

  @Override
  Pair<IsEncryptionEnabledResponse, Object> deserialize(
          CallResponse callResponse, String uuid) throws Exception {
    final MasterEncryptionOuterClass.IsEncryptionEnabledResponsePB.Builder respBuilder =
            MasterEncryptionOuterClass.IsEncryptionEnabledResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    MasterTypes.MasterErrorPB serverError = respBuilder.hasError() ? respBuilder.getError() : null;
    IsEncryptionEnabledResponse response = new IsEncryptionEnabledResponse(
            deadlineTracker.getElapsedMillis(), uuid, respBuilder.getEncryptionEnabled(),
            respBuilder.getKeyId(), serverError);
    return new Pair<IsEncryptionEnabledResponse, Object>(response, serverError);
  }
}
