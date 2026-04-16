package org.yb.client;

import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;

@InterfaceAudience.Public
public class SnapshotInfo {

    private UUID snapshotUUID;
    private long snapshotTime;
    private long previousSnapshotTime;
    private State state;

    public SnapshotInfo(UUID snapshotUUID, long snapshotTime,
                        long previousSnapshotTime, State state) {
        this.snapshotUUID = snapshotUUID;
        this.snapshotTime = snapshotTime;
        this.previousSnapshotTime = previousSnapshotTime;
        this.state = state;
    }

    public UUID getSnapshotUUID() {
        return snapshotUUID;
    }

    public long getSnapshotTime() {
        return snapshotTime;
    }

    public long getPreviousSnapshotTime() {
        return previousSnapshotTime;
    }

    public State getState() {
        return state;
    }
}
