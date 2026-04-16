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
import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterBackupOuterClass.EditSnapshotScheduleRequestPB;
import org.yb.master.MasterBackupOuterClass.EditSnapshotScheduleResponsePB;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.util.CommonUtil;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class EditSnapshotScheduleRequest extends YRpc<EditSnapshotScheduleResponse> {

  private final UUID snapshotScheduleUUID;
  private final long retentionInSecs;
  private final long timeIntervalInSecs;

  EditSnapshotScheduleRequest(
      YBTable table,
      UUID snapshotScheduleUUID,
      long retentionInSecs,
      long timeIntervalInSecs) {
    super(table);
    this.snapshotScheduleUUID = snapshotScheduleUUID;
    this.retentionInSecs = retentionInSecs;
    this.timeIntervalInSecs = timeIntervalInSecs;
  }

  @Override
  ByteBuf serialize(Message header) {
    assert header.isInitialized();
    final EditSnapshotScheduleRequestPB.Builder builder =
        EditSnapshotScheduleRequestPB.newBuilder();
    builder.setSnapshotScheduleId(CommonUtil.convertToByteString(snapshotScheduleUUID));
    builder.setIntervalSec(timeIntervalInSecs);
    builder.setRetentionDurationSec(retentionInSecs);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() {
    return MASTER_BACKUP_SERVICE_NAME;
  }

  @Override
  String method() {
    return "EditSnapshotSchedule";
  }

  @Override
  Pair<EditSnapshotScheduleResponse, Object> deserialize(
      CallResponse callResponse,
      String masterUUID) throws Exception {
    final EditSnapshotScheduleResponsePB.Builder respBuilder =
        EditSnapshotScheduleResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);

    MasterErrorPB serverError = respBuilder.hasError() ? respBuilder.getError() : null;
    UUID scheduleUuid = CommonUtil.convertToUUID(respBuilder.getSchedule().getId());
    EditSnapshotScheduleResponse response =
        new EditSnapshotScheduleResponse(
            deadlineTracker.getElapsedMillis(),
            masterUUID,
            serverError,
            scheduleUuid,
            respBuilder.getSchedule().getOptions().getRetentionDurationSec(),
            respBuilder.getSchedule().getOptions().getIntervalSec());
    return new Pair<>(response, serverError);
  }
}
