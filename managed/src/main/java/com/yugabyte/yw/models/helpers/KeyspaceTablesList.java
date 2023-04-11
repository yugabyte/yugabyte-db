package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.common.BackupUtil;
import java.util.List;
import java.util.UUID;
import lombok.Builder;
import lombok.Value;

@Value
@Builder
public class KeyspaceTablesList {
  String keyspace;
  List<String> tablesList;
  List<UUID> tableUUIDList;
  Long backupSizeInBytes;
  String defaultLocation;
  List<BackupUtil.RegionLocations> perRegionLocations;
}
