package org.yb.client;

import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes;

@InterfaceAudience.Public
public class RestoreSnapshotResponse extends YRpcResponse {

    private final MasterTypes.MasterErrorPB serverError;
    private UUID restorationUUID;

    RestoreSnapshotResponse(long ellapsedMillis,
                            String uuid,
                            MasterTypes.MasterErrorPB serverError,
                            UUID restorationUUID) {
        super(ellapsedMillis, uuid);
        this.serverError = serverError;
        this.restorationUUID = restorationUUID;
    }

    public UUID getRestorationUUID() {
        return restorationUUID;
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
