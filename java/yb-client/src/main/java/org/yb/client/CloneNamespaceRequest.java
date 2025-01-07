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

import static org.yb.util.HybridTimeUtil.clockTimestampToHTTimestamp;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import java.util.concurrent.TimeUnit;
import java.util.Objects;
import java.util.UUID;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterBackupOuterClass.CloneNamespaceRequestPB;
import org.yb.master.MasterBackupOuterClass.CloneNamespaceResponsePB;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.master.MasterTypes.NamespaceIdentifierPB;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class CloneNamespaceRequest extends YRpc<CloneNamespaceResponse> {

  private final YQLDatabase databaseType;
  private final String sourceKeyspaceName;
  private final ByteString keyspaceId;
  private final String targetKeyspaceName;
  private final long cloneTimeInMillis;

  CloneNamespaceRequest(
      YBTable table,
      YQLDatabase databaseType,
      String sourceKeyspaceName,
      String targetKeyspaceName,
      long cloneTimeInMillis) {
    this(table,
         databaseType,
         sourceKeyspaceName,
         null /* keyspaceId */,
         targetKeyspaceName,
         cloneTimeInMillis);
  }

  CloneNamespaceRequest(
      YBTable table,
      YQLDatabase databaseType,
      String sourceKeyspaceName,
      String keyspaceId,
      String targetKeyspaceName,
      long cloneTimeInMillis) {
    super(table);
    this.databaseType = databaseType;
    this.sourceKeyspaceName = sourceKeyspaceName;
    if (Objects.nonNull(keyspaceId)) {
      this.keyspaceId = ByteString.copyFromUtf8(keyspaceId);
    } else {
      this.keyspaceId = null;
    }
    this.targetKeyspaceName = targetKeyspaceName;
    this.cloneTimeInMillis = cloneTimeInMillis;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final CloneNamespaceRequestPB.Builder builder = CloneNamespaceRequestPB.newBuilder();
    NamespaceIdentifierPB.Builder namespaceIdentifierBuilder = NamespaceIdentifierPB.newBuilder();
    namespaceIdentifierBuilder.setDatabaseType(databaseType).setName(sourceKeyspaceName);
    if (Objects.nonNull(keyspaceId)) {
      namespaceIdentifierBuilder.setId(keyspaceId);
    }
    builder.setSourceNamespace(namespaceIdentifierBuilder);
    builder.setRestoreHt(clockTimestampToHTTimestamp(cloneTimeInMillis, TimeUnit.MILLISECONDS));
    builder.setTargetNamespaceName(targetKeyspaceName);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_BACKUP_SERVICE_NAME;
  }

  @Override
  String method() {
    return "CloneNamespace";
  }

  @Override
  Pair<CloneNamespaceResponse, Object> deserialize(
      CallResponse callResponse,
      String masterUUID) throws Exception {
    final CloneNamespaceResponsePB.Builder respBuilder = CloneNamespaceResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);

    MasterErrorPB serverError = respBuilder.hasError() ? respBuilder.getError() : null;
    CloneNamespaceResponse response =
        new CloneNamespaceResponse(
            deadlineTracker.getElapsedMillis(),
            masterUUID,
            serverError,
            respBuilder.getSourceNamespaceId(),
            respBuilder.getSeqNo());
    return new Pair<>(response, serverError);
  }
}
