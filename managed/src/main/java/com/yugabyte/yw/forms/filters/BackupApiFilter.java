/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.forms.filters;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.filters.BackupFilter;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.Set;
import java.util.UUID;
import lombok.Data;
import lombok.NoArgsConstructor;
import org.apache.commons.collections.CollectionUtils;

@Data
@NoArgsConstructor
public class BackupApiFilter {

  @ApiModelProperty(value = "The start date for backup filter.", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date dateRangeStart;

  @ApiModelProperty(value = "The end date for backup filter.", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date dateRangeEnd;

  private Set<Backup.BackupState> states;
  private Set<String> keyspaceList;
  private Set<String> universeNameList;
  private Set<UUID> storageConfigUUIDList;
  private Set<UUID> scheduleUUIDList;
  private Set<UUID> universeUUIDList;
  private boolean onlyShowDeletedUniverses;
  private boolean onlyShowDeletedConfigs;

  public BackupFilter toFilter() {
    BackupFilter.BackupFilterBuilder builder = BackupFilter.builder();
    if (!CollectionUtils.isEmpty(storageConfigUUIDList)) {
      builder.storageConfigUUIDList(storageConfigUUIDList);
    }
    if (!CollectionUtils.isEmpty(universeUUIDList)) {
      builder.universeUUIDList(universeUUIDList);
    }
    if (!CollectionUtils.isEmpty(scheduleUUIDList)) {
      builder.scheduleUUIDList(scheduleUUIDList);
    }
    if (!CollectionUtils.isEmpty(universeNameList)) {
      builder.universeNameList(universeNameList);
    }
    if (!CollectionUtils.isEmpty(keyspaceList)) {
      builder.keyspaceList(keyspaceList);
    }
    if (!CollectionUtils.isEmpty(states)) {
      builder.states(states);
    }
    if (dateRangeEnd != null) {
      builder.dateRangeEnd(dateRangeEnd);
    }
    if (dateRangeStart != null) {
      builder.dateRangeStart(dateRangeStart);
    }
    if (onlyShowDeletedUniverses) {
      builder.onlyShowDeletedUniverses(onlyShowDeletedUniverses);
    }
    if (onlyShowDeletedConfigs) {
      builder.onlyShowDeletedConfigs(onlyShowDeletedConfigs);
    }

    return builder.build();
  }
}
