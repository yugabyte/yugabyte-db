/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.models.filters;

import com.yugabyte.yw.models.Backup;
import java.util.Date;
import java.util.Set;
import java.util.UUID;
import lombok.Builder;
import lombok.Value;

@Value
@Builder(toBuilder = true)
public class BackupFilter {

  Date dateRangeStart;
  Date dateRangeEnd;
  Set<Backup.BackupState> states;
  Set<String> keyspaceList;
  Set<String> universeNameList;
  Set<UUID> storageConfigUUIDList;
  Set<UUID> scheduleUUIDList;
  Set<UUID> universeUUIDList;
  Set<UUID> backupUUIDList;
  UUID customerUUID;
  boolean onlyShowDeletedUniverses;
  boolean onlyShowDeletedConfigs;
}
