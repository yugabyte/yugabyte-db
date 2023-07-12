package org.yb.client;

import java.util.List;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes;

@InterfaceAudience.Public
public class ListSnapshotRestorationsResponse extends YRpcResponse {

    private List<SnapshotRestorationInfo> snapshotRestorationInfoList;

    ListSnapshotRestorationsResponse(long ellapsedMillis,
                                    String uuid,
                                    List<SnapshotRestorationInfo> snapshotRestorationInfoList) {
        super(ellapsedMillis, uuid);
        this.snapshotRestorationInfoList = snapshotRestorationInfoList;
    }

    public List<SnapshotRestorationInfo> getSnapshotRestorationInfoList() {
        return snapshotRestorationInfoList;
    }
}
