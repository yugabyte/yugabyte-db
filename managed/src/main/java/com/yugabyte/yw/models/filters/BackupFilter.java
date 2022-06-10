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

import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertConfiguration.Severity;
import com.yugabyte.yw.models.AlertLabel;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import java.util.Arrays;
import java.util.Collection;
import java.util.Date;
import java.util.EnumSet;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import lombok.Builder;
import lombok.NonNull;
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
  UUID customerUUID;
  boolean onlyShowDeletedUniverses;
  boolean onlyShowDeletedConfigs;
}
