package com.yugabyte.yw.models.helpers;

import java.util.Set;
import lombok.Builder;
import lombok.Value;

@Value
@Builder
public class KeyspaceTablesList {
  String keyspace;
  Set<String> tablesList;
  Long backupSizeInBytes;
  String storageLocation;
}
