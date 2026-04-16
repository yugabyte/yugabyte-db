package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.forms.backuprestore.BackupPointInTimeRestoreWindow;
import java.util.List;
import java.util.UUID;
import lombok.Builder;
import lombok.Value;

@Value
@Builder
public class KeyspaceTablesList {
  String keyspace;
  Boolean allTables;
  List<String> tablesList;
  List<UUID> tableUUIDList;
  Long backupSizeInBytes;
  String defaultLocation;
  List<BackupUtil.RegionLocations> perRegionLocations;
  BackupPointInTimeRestoreWindow backupPointInTimeRestoreWindow;
}
