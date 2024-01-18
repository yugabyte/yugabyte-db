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

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import java.util.Collections;
import java.util.Objects;
import java.util.UUID;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterBackupOuterClass.CreateSnapshotScheduleRequestPB;
import org.yb.master.MasterBackupOuterClass.CreateSnapshotScheduleResponsePB;
import org.yb.master.MasterBackupOuterClass.SnapshotScheduleFilterPB;
import org.yb.master.MasterBackupOuterClass.SnapshotScheduleOptionsPB;
import org.yb.master.MasterBackupOuterClass.TableIdentifiersPB;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.master.MasterTypes.NamespaceIdentifierPB;
import org.yb.master.MasterTypes.TableIdentifierPB;
import org.yb.util.CommonUtil;
import org.yb.util.Pair;
import org.yb.util.SnapshotUtil;

@InterfaceAudience.Public
public class CreateSnapshotScheduleRequest extends YRpc<CreateSnapshotScheduleResponse> {

  private final YQLDatabase databaseType;
  private final String keyspaceName;
  private final ByteString keyspaceId;
  private final long retentionInSecs;
  private final long timeIntervalInSecs;

  CreateSnapshotScheduleRequest(
      YBTable table,
      YQLDatabase databaseType,
      String keyspaceName,
      long retentionInSecs,
      long timeIntervalInSecs) {
    this(
      table,
      databaseType,
      keyspaceName,
      null /* keyspaceId */,
      retentionInSecs,
      timeIntervalInSecs);
  }

  CreateSnapshotScheduleRequest(
      YBTable table,
      YQLDatabase databaseType,
      String keyspaceName,
      String keyspaceId,
      long retentionInSecs,
      long timeIntervalInSecs) {
    super(table);
    this.databaseType = databaseType;
    this.keyspaceName = keyspaceName;
    if (Objects.nonNull(keyspaceId)) {
      this.keyspaceId = ByteString.copyFromUtf8(keyspaceId);
    } else {
      this.keyspaceId = null;
    }
    this.retentionInSecs = retentionInSecs;
    this.timeIntervalInSecs = timeIntervalInSecs;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final CreateSnapshotScheduleRequestPB.Builder builder =
        CreateSnapshotScheduleRequestPB.newBuilder();

    NamespaceIdentifierPB.Builder namespaceIdentifierBuilder = NamespaceIdentifierPB.newBuilder();
    namespaceIdentifierBuilder.setDatabaseType(this.databaseType)
        .setName(this.keyspaceName);
    if (Objects.nonNull(this.keyspaceId)) {
      namespaceIdentifierBuilder.setId(this.keyspaceId);
    }

    TableIdentifierPB tableIdentifier = TableIdentifierPB.newBuilder()
        .setNamespace(namespaceIdentifierBuilder.build())
        .build();
    TableIdentifiersPB tableIdentifiers = TableIdentifiersPB.newBuilder()
        .addAllTables(Collections.singletonList(tableIdentifier))
        .build();
    SnapshotScheduleOptionsPB.Builder snapshotOptionsBuilder =
        SnapshotScheduleOptionsPB.newBuilder();
    snapshotOptionsBuilder.setFilter(SnapshotScheduleFilterPB.newBuilder()
        .setTables(tableIdentifiers)
        .build());
    snapshotOptionsBuilder.setIntervalSec(timeIntervalInSecs);
    snapshotOptionsBuilder.setRetentionDurationSec(retentionInSecs);
    builder.setOptions(snapshotOptionsBuilder.build());

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_BACKUP_SERVICE_NAME;
  }

  @Override
  String method() {
    return "CreateSnapshotSchedule";
  }

  @Override
  Pair<CreateSnapshotScheduleResponse, Object> deserialize(
      CallResponse callResponse,
      String masterUUID) throws Exception {
    final CreateSnapshotScheduleResponsePB.Builder respBuilder =
        CreateSnapshotScheduleResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);

    MasterErrorPB serverError = respBuilder.hasError() ? respBuilder.getError() : null;
    UUID scheduleUuid = CommonUtil.convertToUUID(respBuilder.getSnapshotScheduleId());
    CreateSnapshotScheduleResponse response =
        new CreateSnapshotScheduleResponse(
            deadlineTracker.getElapsedMillis(),
            masterUUID,
            serverError,
            scheduleUuid);
    return new Pair<>(response, serverError);
  }
}
