package org.yb.client;

import java.util.List;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes;

@InterfaceAudience.Public
public class ListSnapshotsResponse extends YRpcResponse {

    private List<SnapshotInfo> snapshotInfoList;
    private MasterTypes.MasterErrorPB serverError;

    ListSnapshotsResponse(long ellapsedMillis,
                            String uuid,
                            MasterTypes.MasterErrorPB serverError,
                            List<SnapshotInfo> snapshotInfoList) {
        super(ellapsedMillis, uuid);
        this.serverError = serverError;
        this.snapshotInfoList = snapshotInfoList;
    }

    public List<SnapshotInfo> getSnapshotInfoList() {
        return snapshotInfoList;
    }

    public boolean hasError() {
        return serverError != null;
    }

    public String errorMessage() {
        if (serverError == null) {
            return "";
        }
        return serverError.getStatus().getMessage();
    }
}
