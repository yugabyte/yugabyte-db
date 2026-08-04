package org.yb.client;

import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes;

@InterfaceAudience.Public
public class EditSnapshotScheduleResponse extends YRpcResponse {
    private final MasterTypes.MasterErrorPB serverError;

    private UUID snapshotScheduleUUID;
    private long retentionDurationInSecs;
    private long intervalInSecs;

    EditSnapshotScheduleResponse(long ellapsedMillis,
                                    String uuid,
                                    MasterTypes.MasterErrorPB serverError,
                                    UUID snapshotScheduleUUID,
                                    long retentionDurationInSecs,
                                    long intervalInSecs) {
        super(ellapsedMillis, uuid);
        this.serverError = serverError;
        this.snapshotScheduleUUID = snapshotScheduleUUID;
        this.retentionDurationInSecs = retentionDurationInSecs;
        this.intervalInSecs = intervalInSecs;
    }

    public UUID getSnapshotScheduleUUID() {
        return snapshotScheduleUUID;
    }

    public long getIntervalInSecs() {
        return intervalInSecs;
    }

    public long getRetentionDurationInSecs() {
        return retentionDurationInSecs;
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
