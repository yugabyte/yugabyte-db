// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.filters;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.filters.RestoreFilter;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.Set;
import java.util.UUID;
import lombok.Data;
import lombok.NoArgsConstructor;
import org.apache.commons.collections.CollectionUtils;

@Data
@NoArgsConstructor
public class RestoreApiFilter {

  @ApiModelProperty(
      value = "The start date to filter paged query.",
      example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date dateRangeStart;

  @ApiModelProperty(value = "The end date to filter paged query.", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
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
