// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.paging.PagedQuery;
import com.yugabyte.yw.models.paging.PagedQuery.SortByIF;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import java.util.UUID;
import lombok.Data;
import lombok.Getter;

@Data
@ApiModel("PA Collector registered universe info")
public class PaUniverseInfo {

  @ApiModelProperty(value = "Universe UUID")
  private UUID universeUuid;

  @ApiModelProperty(value = "Universe name (from YBA, or null if universe was deleted)")
  private String universeName;

  @ApiModelProperty(value = "Data mount points")
  private List<String> dataMountPoints;

  @ApiModelProperty(value = "Other mount points")
  private List<String> otherMountPoints;

  @ApiModelProperty(value = "Whether advanced observability (metrics export) is enabled")
  private boolean advancedObservability;

  @Getter
  public enum SortBy implements PagedQuery.SortByIF {
    universeName("universeName"),
    universeUuid("universeUuid");

    private final String sortField;

    SortBy(String sortField) {
      this.sortField = sortField;
    }

    @Override
    public SortByIF getOrderField() {
      return universeUuid;
    }
  }
}
