package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.models.Schedule.State;
import com.yugabyte.yw.models.helpers.KeyspaceTablesList;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import io.swagger.annotations.ApiModelProperty;
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
  boolean useLocalTimezone;
  String scheduleName;

  @ApiModelProperty(value = "Previous completed task time", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  Date prevCompletedTask;

  @ApiModelProperty(value = "Next expected task time", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
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
    boolean pointInTimeRestoreEnabled;
    boolean useRoles;
  }
}
