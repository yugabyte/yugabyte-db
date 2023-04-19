package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.common.BackupUtil;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import lombok.Builder;
import lombok.Value;

@Value
@Builder
public class KeyspaceTablesList {
  String keyspace;
  Boolean allTables;
  Set<String> tablesList;
  Set<UUID> tableUUIDList;
  Long backupSizeInBytes;
  String defaultLocation;
  List<BackupUtil.RegionLocations> perRegionLocations;
}
