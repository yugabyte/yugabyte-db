package org.yb.client;

import java.util.List;
import java.util.UUID;
import org.yb.annotations.InterfaceAudience;
import org.yb.CommonTypes.YQLDatabase;

@InterfaceAudience.Public
public class SnapshotScheduleInfo {

  private UUID snapshotScheduleUUID;
  private long intervalInSecs;
  private long retentionDurationInSecs;
  private List<SnapshotInfo> snapshotInfoList;
  private String namespaceName;
  private UUID namespaceUUID;
  private YQLDatabase dbType;

  public SnapshotScheduleInfo(
      UUID snapshotScheduleUUID,
      long intervalInSecs,
      long retentionDurationInSecs,
      List<SnapshotInfo> snapshotInfoList,
      String namespaceName,
      UUID namespaceUUID,
      YQLDatabase dbType) {
    this.snapshotScheduleUUID = snapshotScheduleUUID;
    this.intervalInSecs = intervalInSecs;
    this.snapshotInfoList = snapshotInfoList;
    this.namespaceName = namespaceName;
    this.namespaceUUID = namespaceUUID;
    this.dbType = dbType;
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

  public String getNamespaceName() {
    return namespaceName;
  }

  public UUID getNamespaceUUID() {
    return namespaceUUID;
  }

  public YQLDatabase getDbType() {
    return dbType;
  }
}
