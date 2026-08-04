package org.yb.client;

import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes;

@InterfaceAudience.Public
public class RestoreSnapshotScheduleResponse extends YRpcResponse {

    private final MasterTypes.MasterErrorPB serverError;
    private UUID restorationUUID;
    private UUID snapshotUUID;

    RestoreSnapshotScheduleResponse(long ellapsedMillis,
                            String uuid,
                            MasterTypes.MasterErrorPB serverError,
                            UUID restorationUUID,
                            UUID snapshotUUID) {
        super(ellapsedMillis, uuid);
        this.serverError = serverError;
        this.restorationUUID = restorationUUID;
        this.snapshotUUID = snapshotUUID;
    }

    public UUID getRestorationUUID() {
        return restorationUUID;
    }

    public UUID getSnapshotUUID() {
        return snapshotUUID;
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
