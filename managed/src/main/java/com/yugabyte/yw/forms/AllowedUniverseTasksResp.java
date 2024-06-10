// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.AllowedTasks;
import java.util.Set;
import java.util.stream.Collectors;

public class AllowedUniverseTasksResp {
  @JsonIgnore public final AllowedTasks allowedTasks;

  public AllowedUniverseTasksResp(AllowedTasks allowedTasks) {
    this.allowedTasks = allowedTasks;
  }

  @JsonProperty("restricted")
  public boolean isRestricted() {
    return allowedTasks.isRestricted();
  }

  @JsonProperty("taskIds")
  public Set<String> getTaskIds() {
    return allowedTasks.getTaskTypes().stream()
        .flatMap(t -> t.getCustomerTaskIds().stream())
        .map(p -> p.getFirst() + "_" + p.getSecond())
        .collect(Collectors.toSet());
  }
}
