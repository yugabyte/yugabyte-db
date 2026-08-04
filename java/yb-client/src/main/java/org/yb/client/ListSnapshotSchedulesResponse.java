package org.yb.client;

import java.util.List;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes;

@InterfaceAudience.Public
public class ListSnapshotSchedulesResponse extends YRpcResponse {

    private List<SnapshotScheduleInfo> snapshotScheduleInfoList;
    private MasterTypes.MasterErrorPB serverError;

    ListSnapshotSchedulesResponse(long ellapsedMillis,
                                    String uuid,
                                    MasterTypes.MasterErrorPB serverError,
                                    List<SnapshotScheduleInfo> snapshotScheduleInfoList) {
        super(ellapsedMillis, uuid);
        this.serverError = serverError;
        this.snapshotScheduleInfoList = snapshotScheduleInfoList;
    }

    public List<SnapshotScheduleInfo> getSnapshotScheduleInfoList() {
        return snapshotScheduleInfoList;
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
