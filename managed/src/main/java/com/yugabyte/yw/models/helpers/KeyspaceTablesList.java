package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.common.BackupUtil;
import java.util.List;
import java.util.Set;
import lombok.Builder;
import lombok.Value;

@Value
@Builder
public class KeyspaceTablesList {
  String keyspace;
  Set<String> tablesList;
  Long backupSizeInBytes;
  String defaultLocation;
  List<BackupUtil.RegionLocations> perRegionLocations;
}
