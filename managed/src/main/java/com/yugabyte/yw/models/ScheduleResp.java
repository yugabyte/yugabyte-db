package com.yugabyte.yw.models;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.models.Schedule.State;
import com.yugabyte.yw.models.helpers.KeyspaceTablesList;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import lombok.Builder;
import lombok.Value;
import org.yb.CommonTypes.TableType;

@Value
@Builder
public class ScheduleResp {
  UUID scheduleUUID;
  UUID customerUUID;
  int failureCount;
  TaskType taskType;
  State status;
  String cronExpression;
  String scheduleName;
  Date prevCompletedTask;
  Date nextExpectedTask;
  long frequency;
  TimeUnit frequencyTimeUnit;
  boolean runningState;
  BackupInfo backupInfo;
  JsonNode taskParams;
  boolean backlogStatus;
  boolean incrementBacklogStatus;
  long incrementalBackupFrequency;
  TimeUnit incrementalBackupFrequencyTimeUnit;
  Boolean tableByTableBackup;

  @Value
  @Builder
  public static class BackupInfo {
    boolean fullBackup;
    List<KeyspaceTablesList> keyspaceList;
    TableType backupType;
    UUID universeUUID;
    UUID storageConfigUUID;
    long timeBeforeDelete;
    boolean useTablespaces;
    TimeUnit expiryTimeUnit;
    long parallelism;
  }
}
