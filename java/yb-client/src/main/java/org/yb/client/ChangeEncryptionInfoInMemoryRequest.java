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

import io.netty.buffer.ByteBuf;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterEncryptionOuterClass;
import org.yb.util.Pair;

import com.google.protobuf.Message;

@InterfaceAudience.Public
class ChangeEncryptionInfoInMemoryRequest extends YRpc<ChangeEncryptionInfoInMemoryResponse> {
  private String versionId;
  private boolean encryptionEnabled;

  public ChangeEncryptionInfoInMemoryRequest(
          YBTable masterTable, String versionId, boolean encryptionEnabled) {
    super(masterTable);
    this.versionId = versionId;
    this.encryptionEnabled = encryptionEnabled;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final MasterEncryptionOuterClass.ChangeEncryptionInfoRequestPB.Builder builder =
            MasterEncryptionOuterClass.ChangeEncryptionInfoRequestPB.newBuilder()
                    .setEncryptionEnabled(this.encryptionEnabled)
                    .setVersionId(this.versionId)
                    .setInMemory(true);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_SERVICE_NAME;
  }

  @Override
  String method() {
    return "ChangeEncryptionInfo";
  }

  @Override
  Pair<ChangeEncryptionInfoInMemoryResponse, Object> deserialize(
          CallResponse callResponse, String uuid) throws Exception {
    final MasterEncryptionOuterClass.ChangeEncryptionInfoResponsePB.Builder respBuilder =
            MasterEncryptionOuterClass.ChangeEncryptionInfoResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasError = respBuilder.hasError();
    ChangeEncryptionInfoInMemoryResponse response =
            new ChangeEncryptionInfoInMemoryResponse(deadlineTracker.getElapsedMillis(), uuid,
                    hasError ? respBuilder.getErrorBuilder().build() : null);
    return new Pair<ChangeEncryptionInfoInMemoryResponse, Object>(response,
            hasError ? respBuilder.getError() : null);
  }
}
