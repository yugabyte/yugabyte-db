package com.yugabyte.yw.models;

import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.helpers.KeyspaceTablesList;
import org.yb.CommonTypes.TableType;
import java.util.Collection;
import java.util.Date;
import java.util.UUID;
import lombok.Builder;
import lombok.Value;
import java.util.HashSet;
import java.util.Set;

@Value
@Builder
public class BackupResp {

  BackupState state;
  TableType backupType;
  UUID backupUUID;
  UUID storageConfigUUID;
  UUID universeUUID;
  UUID scheduleUUID;
  UUID customerUUID;
  String universeName;
  Boolean isStorageConfigPresent;
  Boolean isUniversePresent;
  Boolean onDemand;
  Date createTime;
  Date updateTime;
  Date expiryTime;
  Set<KeyspaceTablesList> responseList;
}
