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
import java.nio.charset.Charset;
import java.util.Arrays;
import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.master.MasterBackupOuterClass;
import org.yb.master.MasterBackupOuterClass.CreateSnapshotScheduleResponsePB;
import org.yb.master.MasterBackupOuterClass.CreateSnapshotScheduleRequestPB;
import org.yb.master.MasterBackupOuterClass.SnapshotScheduleFilterPB;
import org.yb.master.MasterBackupOuterClass.SnapshotScheduleOptionsPB;
import org.yb.master.MasterBackupOuterClass.TableIdentifiersPB;
import org.yb.master.MasterTypes;
import org.yb.master.MasterTypes.TableIdentifierPB;
import org.yb.util.Pair;
import org.yb.util.SnapshotUtil;

@InterfaceAudience.Public
public class CreateSnapshotScheduleRequest extends YRpc<CreateSnapshotScheduleResponse> {

    private YQLDatabase databaseType;
    private String keyspaceName;
    private long retentionInSecs;
    private long timeIntervalInSecs;

    CreateSnapshotScheduleRequest(YBTable table,
            YQLDatabase databaseType,
            String keyspaceName,
            long retentionInSecs,
            long timeIntervalInSecs) {
        super(table);
        this.databaseType = databaseType;
        this.keyspaceName = keyspaceName;
        this.retentionInSecs = retentionInSecs;
        this.timeIntervalInSecs = timeIntervalInSecs;
    }

    @Override
    ByteBuf serialize(Message header) {
        assert header.isInitialized();
        final CreateSnapshotScheduleRequestPB.Builder builder =
                CreateSnapshotScheduleRequestPB.newBuilder();

        TableIdentifierPB tableID = TableIdentifierPB.newBuilder()
                                .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder()
                                              .setName(this.keyspaceName)
                                              .setDatabaseType(this.databaseType))
                                .build();

        TableIdentifiersPB tableIDList = TableIdentifiersPB.newBuilder()
                                    .addAllTables(Arrays.asList(tableID))
                                    .build();

        SnapshotScheduleOptionsPB.Builder snapshotOptionsBuilder =
                SnapshotScheduleOptionsPB.newBuilder();
        snapshotOptionsBuilder.setFilter(SnapshotScheduleFilterPB.newBuilder()
                                            .setTables(tableIDList)
                                            .build());
        snapshotOptionsBuilder.setIntervalSec(timeIntervalInSecs);
        snapshotOptionsBuilder.setRetentionDurationSec(retentionInSecs);

        builder.setOptions(snapshotOptionsBuilder.build());
        return toChannelBuffer(header, builder.build());
    }

    @Override
    String serviceName() { return MASTER_BACKUP_SERVICE_NAME; }

    @Override
    String method() {
        return "CreateSnapshotSchedule";
    }

    @Override
    Pair<CreateSnapshotScheduleResponse, Object> deserialize(CallResponse callResponse,
                                                   String masterUUID) throws Exception {

        final CreateSnapshotScheduleResponsePB.Builder respBuilder =
                CreateSnapshotScheduleResponsePB.newBuilder();
        readProtobuf(callResponse.getPBMessage(), respBuilder);

        UUID scheduleUUID = SnapshotUtil.convertToUUID(respBuilder.getSnapshotScheduleId());
        MasterTypes.MasterErrorPB serverError =
                respBuilder.hasError() ? respBuilder.getError() : null;
        CreateSnapshotScheduleResponse response =
                new CreateSnapshotScheduleResponse(deadlineTracker.getElapsedMillis(),
                        masterUUID, serverError, scheduleUUID);
        return new Pair<CreateSnapshotScheduleResponse, Object>(response, serverError);
    }
}
