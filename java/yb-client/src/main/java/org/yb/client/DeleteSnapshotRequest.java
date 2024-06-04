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

package org.yb.client;

import com.google.protobuf.Message;
import io.netty.buffer.ByteBuf;
import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterBackupOuterClass.DeleteSnapshotRequestPB;
import org.yb.master.MasterBackupOuterClass.DeleteSnapshotResponsePB;
import org.yb.master.MasterTypes;
import org.yb.util.CommonUtil;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class DeleteSnapshotRequest extends YRpc<DeleteSnapshotResponse>{

    private UUID snapshotUUID;

    DeleteSnapshotRequest(YBTable table, UUID snapshotUUID) {
        super(table);
        this.snapshotUUID = snapshotUUID;
    }

    @Override
    ByteBuf serialize(Message header) {
        assert header.isInitialized();
        final DeleteSnapshotRequestPB.Builder builder =
                DeleteSnapshotRequestPB.newBuilder();
        builder.setSnapshotId(CommonUtil.convertToByteString(snapshotUUID));
        return toChannelBuffer(header, builder.build());
    }

    @Override
    String serviceName() {
        return MASTER_BACKUP_SERVICE_NAME;
    }

    @Override
    String method() {
        return "DeleteSnapshot";
    }

    @Override
    Pair<DeleteSnapshotResponse, Object> deserialize(CallResponse callResponse, String masterUUID)
            throws Exception {
        final DeleteSnapshotResponsePB.Builder respBuilder =
                DeleteSnapshotResponsePB.newBuilder();
        readProtobuf(callResponse.getPBMessage(), respBuilder);
        MasterTypes.MasterErrorPB serverError =
                respBuilder.hasError() ? respBuilder.getError() : null;
        DeleteSnapshotResponse response =
                new DeleteSnapshotResponse(deadlineTracker.getElapsedMillis(),
                        masterUUID, serverError);
        return new Pair<DeleteSnapshotResponse, Object>(response, serverError);
    }
}
