package org.yb.client;

import java.util.List;
import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;

@InterfaceAudience.Public
public class SnapshotScheduleInfo {

    private UUID snapshotScheduleUUID;
    private long intervalInSecs;
    private long retentionDurationInSecs;
    private List<SnapshotInfo> snapshotInfoList;

    public SnapshotScheduleInfo(UUID snapshotScheduleUUID,
            long intervalInSecs,
            long retentionDurationInSecs,
            List<SnapshotInfo> snapshotInfoList) {
        this.snapshotScheduleUUID = snapshotScheduleUUID;
        this.intervalInSecs = intervalInSecs;
        this.snapshotInfoList = snapshotInfoList;
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

    public List<SnapshotInfo> getSnapshotInfoList() {
        return snapshotInfoList;
    }
}
