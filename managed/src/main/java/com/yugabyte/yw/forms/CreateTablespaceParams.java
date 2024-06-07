// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.yugabyte.yw.common.TableSpaceStructures.TableSpaceInfo;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import javax.validation.Valid;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;

public class CreateTablespaceParams {
  @JsonAlias({"tablespaces"})
  @ApiModelProperty(value = "Tablespaces to be created", required = true)
  @Valid
  @NotNull
  @Size(min = 1)
  public List<TableSpaceInfo> tablespaceInfos;
}
