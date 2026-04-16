package org.yb.client;

import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes;

@InterfaceAudience.Public
public class CreateSnapshotScheduleResponse extends YRpcResponse {
    private final MasterTypes.MasterErrorPB serverError;

    private UUID snapshotScheduleUUID;

    CreateSnapshotScheduleResponse(long ellapsedMillis,
                                    String uuid,
                                    MasterTypes.MasterErrorPB serverError,
                                    UUID snapshotScheduleUUID) {
        super(ellapsedMillis, uuid);
        this.serverError = serverError;
        this.snapshotScheduleUUID = snapshotScheduleUUID;
    }

    public UUID getSnapshotScheduleUUID() {
        return snapshotScheduleUUID;
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
