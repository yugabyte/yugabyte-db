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
import org.yb.master.MasterBackupOuterClass.RestoreSnapshotResponsePB;
import org.yb.master.MasterBackupOuterClass.RestoreSnapshotRequestPB;
import org.yb.master.MasterTypes;
import org.yb.util.Pair;
import org.yb.util.SnapshotUtil;

@InterfaceAudience.Public
public class RestoreSnapshotRequest extends YRpc<RestoreSnapshotResponse> {

    private UUID snapshotUUID;
    private long restoreTimeInMillis;

    RestoreSnapshotRequest(YBTable table,
        UUID snapshotUUID,
        long restoreHybridTime) {
        super(table);
        this.snapshotUUID = snapshotUUID;
        this.restoreTimeInMillis = restoreTimeInMillis;
    }

    @Override
    ByteBuf serialize(Message header) {
        assert header.isInitialized();
        final RestoreSnapshotRequestPB.Builder builder = RestoreSnapshotRequestPB.newBuilder();
        if (snapshotUUID != null) {
            builder.setSnapshotId(SnapshotUtil.convertToByteString(snapshotUUID));
        }
        builder.setRestoreHt(
            clockTimestampToHTTimestamp(restoreTimeInMillis, TimeUnit.MILLISECONDS));
        return toChannelBuffer(header, builder.build());
    }

    @Override
    String serviceName() { return MASTER_BACKUP_SERVICE_NAME; }

    @Override
    String method() {
        return "RestoreSnapshot";
    }

    @Override
    Pair<RestoreSnapshotResponse, Object> deserialize(CallResponse callResponse,
                                                   String masterUUID) throws Exception {

        final RestoreSnapshotResponsePB.Builder respBuilder =
                RestoreSnapshotResponsePB.newBuilder();
        readProtobuf(callResponse.getPBMessage(), respBuilder);
        MasterTypes.MasterErrorPB serverError =
                respBuilder.hasError() ? respBuilder.getError() : null;

        UUID restorationUUID = SnapshotUtil.convertToUUID(respBuilder.getRestorationId());
        RestoreSnapshotResponse response =
                new RestoreSnapshotResponse(deadlineTracker.getElapsedMillis(),
                        masterUUID, serverError, restorationUUID);
        return new Pair<RestoreSnapshotResponse, Object>(response, serverError);
    }

}
