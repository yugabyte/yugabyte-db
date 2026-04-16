// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnore;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import org.yb.CommonTypes.TableType;
import play.data.validation.Constraints;

@ApiModel(description = "Clone Namespace parameters")
@NoArgsConstructor
public class CloneNamespaceParams extends UniverseTaskParams {

  @JsonIgnore @Getter @Setter private UUID universeUUID;

  @JsonIgnore public UUID customerUUID;

  @ApiModelProperty(value = "PITR Config UUID")
  @Constraints.Required
  public UUID pitrConfigUUID;

  @ApiModelProperty(value = "Clone namespace name")
  @Constraints.Required
  public String targetKeyspaceName;

  @JsonIgnore @Getter @Setter public String keyspaceName;

  @JsonIgnore @Getter @Setter public TableType tableType;

  @ApiModelProperty(
      value =
          "Time in millis at which to clone the source namespace. If not specifed, it is cloned to"
              + " current state.")
  public Long cloneTimeInMillis;
}
