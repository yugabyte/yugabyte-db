// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.filters;

import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.filters.RestoreFilter;
import java.util.Date;
import java.util.Set;
import java.util.UUID;
import lombok.Data;
import lombok.NoArgsConstructor;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

@Data
@NoArgsConstructor
public class RestoreApiFilter {

  private Date dateRangeStart;
  private Date dateRangeEnd;
  private Set<Restore.State> states;
  private Set<String> universeNameList;
  private Set<String> sourceUniverseNameList;
  private Set<UUID> storageConfigUUIDList;
  private Set<UUID> universeUUIDList;
  private boolean onlyShowDeletedSourceUniverses;

  public RestoreFilter toFilter() {
    RestoreFilter.RestoreFilterBuilder builder = RestoreFilter.builder();
    if (!CollectionUtils.isEmpty(storageConfigUUIDList)) {
      builder.storageConfigUUIDList(storageConfigUUIDList);
    }
    if (!CollectionUtils.isEmpty(universeUUIDList)) {
      builder.universeUUIDList(universeUUIDList);
    }
    if (!CollectionUtils.isEmpty(universeNameList)) {
      builder.universeNameList(universeNameList);
    }
    if (!CollectionUtils.isEmpty(sourceUniverseNameList)) {
      builder.sourceUniverseNameList(sourceUniverseNameList);
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
    if (onlyShowDeletedSourceUniverses) {
      builder.onlyShowDeletedSourceUniverses(onlyShowDeletedSourceUniverses);
    }

    return builder.build();
  }
}
