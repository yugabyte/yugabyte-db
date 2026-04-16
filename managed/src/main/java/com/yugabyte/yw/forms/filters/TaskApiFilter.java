// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms.filters;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.filters.TaskFilter;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.Set;
import java.util.UUID;
import lombok.Data;
import lombok.NoArgsConstructor;
import org.apache.commons.collections4.CollectionUtils;

@Data
@NoArgsConstructor
public class TaskApiFilter {

  @ApiModelProperty(
      value = "The start date to filter paged query.",
      example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date dateRangeStart;

  @ApiModelProperty(value = "The end date to filter paged query.", example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date dateRangeEnd;

  private Set<CustomerTask.TargetType> targetList;
  private Set<UUID> targetUUIDList;
  private Set<CustomerTask.TaskType> typeList;
  private Set<String> typeNameList;
  private Set<TaskInfo.State> status;

  public TaskFilter toFilter() {
    TaskFilter.TaskFilterBuilder builder = TaskFilter.builder();
    if (!CollectionUtils.isEmpty(targetList)) {
      builder.targetList(targetList);
    }
    if (!CollectionUtils.isEmpty(targetUUIDList)) {
      builder.targetUUIDList(targetUUIDList);
    }
    if (!CollectionUtils.isEmpty(typeList)) {
      builder.typeList(typeList);
    }
    if (!CollectionUtils.isEmpty(typeNameList)) {
      builder.typeNameList(typeNameList);
    }
    if (!CollectionUtils.isEmpty(status)) {
      builder.status(status);
    }
    if (dateRangeEnd != null) {
      builder.dateRangeEnd(dateRangeEnd);
    }
    if (dateRangeStart != null) {
      builder.dateRangeStart(dateRangeStart);
    }
    return builder.build();
  }
}
