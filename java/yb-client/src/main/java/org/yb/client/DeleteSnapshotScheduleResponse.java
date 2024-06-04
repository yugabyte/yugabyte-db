package org.yb.client;

import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes;

@InterfaceAudience.Public
public class DeleteSnapshotScheduleResponse extends YRpcResponse {

    private MasterTypes.MasterErrorPB serverError;

    DeleteSnapshotScheduleResponse(long ellapsedMillis,
                                    String uuid,
                                    MasterTypes.MasterErrorPB serverError) {
        super(ellapsedMillis, uuid);
        this.serverError = serverError;
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
