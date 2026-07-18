package org.yb.client;

import com.google.protobuf.Message;
import com.google.protobuf.ByteString;
import io.netty.buffer.ByteBuf;
import java.io.UnsupportedEncodingException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterBackupOuterClass;
import org.yb.master.MasterBackupOuterClass.DeleteSnapshotScheduleResponsePB;
import org.yb.master.MasterBackupOuterClass.DeleteSnapshotScheduleRequestPB;
import org.yb.master.MasterTypes;
import org.yb.util.CommonUtil;
import org.yb.util.Pair;

@InterfaceAudience.Public
public class DeleteSnapshotScheduleRequest extends YRpc<DeleteSnapshotScheduleResponse> {

    private UUID snapshotScheduleUUID;

    DeleteSnapshotScheduleRequest(YBTable table, UUID snapshotScheduleUUID) {
        super(table);
        this.snapshotScheduleUUID = snapshotScheduleUUID;
    }

    @Override
    ByteBuf serialize(Message header) {
        assert header.isInitialized();
        final DeleteSnapshotScheduleRequestPB.Builder builder =
                DeleteSnapshotScheduleRequestPB.newBuilder();
        builder.setSnapshotScheduleId(CommonUtil.convertToByteString(snapshotScheduleUUID));
        return toChannelBuffer(header, builder.build());
    }

    @Override
    String serviceName() { return MASTER_BACKUP_SERVICE_NAME; }

    @Override
    String method() {
        return "DeleteSnapshotSchedule";
    }

    @Override
    Pair<DeleteSnapshotScheduleResponse, Object> deserialize(CallResponse callResponse,
                                                   String masterUUID) throws Exception {

        final DeleteSnapshotScheduleResponsePB.Builder respBuilder =
                DeleteSnapshotScheduleResponsePB.newBuilder();
        readProtobuf(callResponse.getPBMessage(), respBuilder);
        MasterTypes.MasterErrorPB serverError =
                respBuilder.hasError() ? respBuilder.getError() : null;
        DeleteSnapshotScheduleResponse response =
                new DeleteSnapshotScheduleResponse(deadlineTracker.getElapsedMillis(),
                        masterUUID, serverError);
        return new Pair<DeleteSnapshotScheduleResponse, Object>(response, serverError);
    }
}
