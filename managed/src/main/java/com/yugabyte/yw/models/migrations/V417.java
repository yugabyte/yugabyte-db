package com.yugabyte.yw.models.migrations;

import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import java.util.UUID;
import lombok.Data;
import lombok.EqualsAndHashCode;

public class V417 {

  @Entity
  @Data
  @EqualsAndHashCode(callSuper = false)
  public static class Backup extends Model {
    public static final Finder<UUID, Backup> find = new Finder<>(Backup.class);
    @Id private UUID backupUUID;
    @DbJson private String backupInfo;
    private BackupState state;
    private long firstSnapshotTime;
  }

  public enum BackupState {
    @EnumValue("In Progress")
    InProgress,

    @EnumValue("Completed")
    Completed,

    @EnumValue("Failed")
    Failed,

    // This state is no longer used in Backup V2 APIs.
    @EnumValue("Deleted")
    Deleted,

    @EnumValue("Skipped")
    Skipped,

    // Complete or partial failure to delete
    @EnumValue("FailedToDelete")
    FailedToDelete,

    @EnumValue("Stopping")
    Stopping,

    @EnumValue("Stopped")
    Stopped,

    @EnumValue("QueuedForDeletion")
    QueuedForDeletion,

    @EnumValue("QueuedForForcedDeletion")
    QueuedForForcedDeletion,

    @EnumValue("DeleteInProgress")
    DeleteInProgress;
  }
}
