package org.yb.client;

import java.util.List;
import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;

@InterfaceAudience.Public
public class SnapshotRestorationInfo {

    private UUID restorationUUID;
    private UUID snapshotUUID;
    private UUID scheduleUUID;
    private long restoreTime;
    private long completionTime;
    private State state;

    public SnapshotRestorationInfo(UUID restorationUUID,
                                   UUID snapshotUUID,
                                   UUID scheduleUUID,
                                   long restoreTime,
                                   long completionTime,
                                   State state) {
        this.restorationUUID = restorationUUID;
        this.snapshotUUID = snapshotUUID;
        this.scheduleUUID = scheduleUUID;
        this.restoreTime = restoreTime;
        this.completionTime = completionTime;
        this.state = state;
    }

    public UUID getRestorationUUID() {
        return restorationUUID;
    }

    public UUID getSnapshotUUID() {
        return snapshotUUID;
    }

    public UUID getScheduleUUID() {
        return scheduleUUID;
    }

    public long getRestoreTime() {
        return restoreTime;
    }

    public long getCompletionTime() {
        return completionTime;
    }

    public State getState() {
        return state;
    }
}
