package org.yb.client;

import static org.yb.util.HybridTimeUtil.clockTimestampToHTTimestamp;

import com.google.protobuf.Message;
import com.google.protobuf.ByteString;
import io.netty.buffer.ByteBuf;
import java.io.UnsupportedEncodingException;
import java.nio.ByteBuffer;
import java.util.concurrent.TimeUnit;
import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterBackupOuterClass;
import org.yb.master.MasterBackupOuterClass.RestoreSnapshotScheduleResponsePB;
import org.yb.master.MasterBackupOuterClass.RestoreSnapshotScheduleRequestPB;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;
import org.yb.util.SnapshotUtil;

@InterfaceAudience.Public
public class RestoreSnapshotScheduleRequest extends YRpc<RestoreSnapshotScheduleResponse> {

    private UUID snapshotScheduleUUID;
    private long restoreTimeInMillis;

    RestoreSnapshotScheduleRequest(YBTable table,
        UUID snapshotScheduleUUID,
        long restoreTimeInMillis) {
        super(table);
        this.snapshotScheduleUUID = snapshotScheduleUUID;
        this.restoreTimeInMillis = restoreTimeInMillis;
    }

    @Override
    ByteBuf serialize(Message header) {
        assert header.isInitialized();
        final RestoreSnapshotScheduleRequestPB.Builder builder =
                RestoreSnapshotScheduleRequestPB.newBuilder();
        if (snapshotScheduleUUID != null) {
            builder.setSnapshotScheduleId(SnapshotUtil.convertToByteString(snapshotScheduleUUID));
        }
        builder.setRestoreHt(
            clockTimestampToHTTimestamp(restoreTimeInMillis, TimeUnit.MILLISECONDS));
        return toChannelBuffer(header, builder.build());
    }

    @Override
    String serviceName() { return MASTER_BACKUP_SERVICE_NAME; }

    @Override
    String method() {
        return "RestoreSnapshotSchedule";
    }

    @Override
    Pair<RestoreSnapshotScheduleResponse, Object> deserialize(CallResponse callResponse,
                                                   String masterUUID) throws Exception {

        final RestoreSnapshotScheduleResponsePB.Builder respBuilder =
                RestoreSnapshotScheduleResponsePB.newBuilder();
        readProtobuf(callResponse.getPBMessage(), respBuilder);
        MasterTypes.MasterErrorPB serverError =
                respBuilder.hasError() ? respBuilder.getError() : null;

        UUID restorationUUID = SnapshotUtil.convertToUUID(respBuilder.getRestorationId());
        UUID snapshotUUID = SnapshotUtil.convertToUUID(respBuilder.getSnapshotId());
        RestoreSnapshotScheduleResponse response =
                new RestoreSnapshotScheduleResponse(deadlineTracker.getElapsedMillis(),
                        masterUUID, serverError, restorationUUID, snapshotUUID);
        return new Pair<RestoreSnapshotScheduleResponse, Object>(response, serverError);
    }
}
