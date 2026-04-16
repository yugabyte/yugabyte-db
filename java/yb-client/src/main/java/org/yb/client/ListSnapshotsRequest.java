package org.yb.client;

import com.google.protobuf.Message;
import com.google.protobuf.ByteString;
import io.netty.buffer.ByteBuf;
import java.io.UnsupportedEncodingException;
import java.nio.ByteBuffer;
import java.util.LinkedList;
import java.util.List;
import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterBackupOuterClass;
import org.yb.master.MasterBackupOuterClass.ListSnapshotsResponsePB;
import org.yb.master.MasterBackupOuterClass.SnapshotInfoPB;
import org.yb.master.MasterTypes;
import org.yb.util.CommonUtil;
import org.yb.util.Pair;
import org.yb.util.SnapshotUtil;

@InterfaceAudience.Public
public class ListSnapshotsRequest extends YRpc<ListSnapshotsResponse> {

    private UUID snapshotUUID;
    private boolean listDeletedSnapshots;

    ListSnapshotsRequest(YBTable table, UUID snapshotUUID, boolean listDeletedSnapshots) {
        super(table);
        this.snapshotUUID = snapshotUUID;
        this.listDeletedSnapshots = listDeletedSnapshots;
    }

    @Override
    ByteBuf serialize(Message header) {
        assert header.isInitialized();
        final MasterBackupOuterClass.ListSnapshotsRequestPB.Builder builder =
                MasterBackupOuterClass.ListSnapshotsRequestPB.newBuilder();

        final MasterBackupOuterClass.ListSnapshotsDetailOptionsPB snapshotsDetailOption =
                MasterBackupOuterClass.ListSnapshotsDetailOptionsPB.newBuilder()
                    .setShowNamespaceDetails(false)
                    .setShowUdtypeDetails(false)
                    .setShowTableDetails(false)
                    .setShowTabletDetails(false)
                    .build();

        if (snapshotUUID != null) {
            builder.setSnapshotId(CommonUtil.convertToByteString(snapshotUUID));
        }
        builder.setListDeletedSnapshots(this.listDeletedSnapshots);
        builder.setDetailOptions(snapshotsDetailOption);
        return toChannelBuffer(header, builder.build());
    }

    @Override
    String serviceName() { return MASTER_BACKUP_SERVICE_NAME; }

    @Override
    String method() {
        return "ListSnapshots";
    }

    @Override
    Pair<ListSnapshotsResponse, Object> deserialize(CallResponse callResponse,
                                                    String masterUUID) throws Exception {

        final ListSnapshotsResponsePB.Builder respBuilder = ListSnapshotsResponsePB.newBuilder();
        readProtobuf(callResponse.getPBMessage(), respBuilder);
        boolean hasErr = respBuilder.hasError();
        MasterTypes.MasterErrorPB serverError =
                hasErr ? respBuilder.getError() : null;

        List<SnapshotInfo> snapshotInfoList = new LinkedList<>();
        if (!hasErr) {
            for (SnapshotInfoPB snapshot: respBuilder.getSnapshotsList()) {
                snapshotInfoList.add(SnapshotUtil.parseSnapshotInfoPB(snapshot));
            }
        }
        ListSnapshotsResponse response =
                new ListSnapshotsResponse(deadlineTracker.getElapsedMillis(),
                        masterUUID, serverError, snapshotInfoList);
        return new Pair<ListSnapshotsResponse, Object>(response, serverError);
    }

}
