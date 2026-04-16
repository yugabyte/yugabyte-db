package org.yb.client;

import com.google.protobuf.Message;
import com.google.protobuf.ByteString;
import io.netty.buffer.ByteBuf;
import java.io.UnsupportedEncodingException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterBackupOuterClass;
import org.yb.master.MasterBackupOuterClass.ListSnapshotRestorationsRequestPB;
import org.yb.master.MasterBackupOuterClass.ListSnapshotRestorationsResponsePB;
import org.yb.master.MasterBackupOuterClass.RestorationInfoPB;
import org.yb.master.MasterTypes;
import org.yb.util.CommonUtil;
import org.yb.util.Pair;
import org.yb.util.SnapshotUtil;

@InterfaceAudience.Public
public class ListSnapshotRestorationsRequest extends YRpc<ListSnapshotRestorationsResponse> {

    private UUID restorationUUID;

    ListSnapshotRestorationsRequest(YBTable table, UUID restorationUUID) {
        super(table);
        this.restorationUUID = restorationUUID;
    }

    @Override
    ByteBuf serialize(Message header) {
        assert header.isInitialized();
        final ListSnapshotRestorationsRequestPB.Builder builder =
                ListSnapshotRestorationsRequestPB.newBuilder();
        if (restorationUUID != null) {
            builder.setRestorationId(CommonUtil.convertToByteString(restorationUUID));
        }
        return toChannelBuffer(header, builder.build());
    }

    @Override
    String serviceName() { return MASTER_BACKUP_SERVICE_NAME; }

    @Override
    String method() {
        return "ListSnapshotRestorations";
    }

    @Override
    Pair<ListSnapshotRestorationsResponse, Object> deserialize(CallResponse callResponse,
                                                   String masterUUID) throws Exception {

        final ListSnapshotRestorationsResponsePB.Builder respBuilder =
                ListSnapshotRestorationsResponsePB.newBuilder();
        readProtobuf(callResponse.getPBMessage(), respBuilder);

        List<SnapshotRestorationInfo> snapshotRestorationInfoList = new ArrayList<>();
        for (RestorationInfoPB restorationInfo: respBuilder.getRestorationsList()) {
            SnapshotRestorationInfo snapshotRestorationInfo =
                SnapshotUtil.parseSnapshotRestorationInfoPB(restorationInfo);
            snapshotRestorationInfoList.add(snapshotRestorationInfo);
        }

        ListSnapshotRestorationsResponse response =
                new ListSnapshotRestorationsResponse(deadlineTracker.getElapsedMillis(),
                        masterUUID, snapshotRestorationInfoList);
        return new Pair<ListSnapshotRestorationsResponse, Object>(response, null);
    }


}
