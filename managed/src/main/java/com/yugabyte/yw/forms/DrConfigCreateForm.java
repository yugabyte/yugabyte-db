package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import java.util.UUID;
import javax.validation.Valid;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import lombok.ToString;
import play.data.validation.Constraints.MaxLength;
import play.data.validation.Constraints.Pattern;
import play.data.validation.Constraints.Required;

@ApiModel(description = "drConfig create form")
@ToString
public class DrConfigCreateForm {

  @Required
  @MaxLength(256)
  @ApiModelProperty(value = "Name", example = "Dr-config1", required = true)
  @Pattern(
      value = "^([^\\s_*<>?|\"\\x00]+)$",
      message =
          "The name of the dr config cannot contain "
              + "[SPACE '_' '*' '<' '>' '?' '|' '\"' NULL] characters")
  public String name;

  @Required
  @ApiModelProperty(value = "Source Universe UUID", required = true)
  public UUID sourceUniverseUUID;

  @Required
  @ApiModelProperty(value = "Target Universe UUID", required = true)
  public UUID targetUniverseUUID;

  @Required
  @ApiModelProperty(
      value = "Source Universe DB IDs",
      example = "[\"0000412b000030008000000000000000\", \"0000412b000030008000000000000001\"]",
      required = true)
  public Set<String> dbs;

  @Valid
  @ApiModelProperty("Parameters needed for the bootstrap flow including backup/restore")
  public XClusterConfigRestartFormData.RestartBootstrapParams bootstrapParams;

  @ApiModelProperty("Run the pre-checks without actually running the subtasks")
  public boolean dryRun = false;

  @Valid
  @ApiModelProperty(
      "Parameters needed for creating PITR configs; if not specified, "
          + "default values from the runtime config will be used")
  public PitrParams pitrParams;

  @ApiModel(description = "PITR parameters")
  @ToString
  @NoArgsConstructor
  @AllArgsConstructor
  public static class PitrParams {
    @Required
    @ApiModelProperty(value = "Retention period of a snapshot in seconds")
    public long retentionPeriodSec;

    @ApiModelProperty(
        value =
            "<b style=\"color:#ff0000\">Deprecated since YBA version 2024.2.0.0.</b> Time interval"
                + " between snapshots in seconds")
    @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2024.2.0.0")
    public long snapshotIntervalSec;
  }

  @ApiModelProperty(hidden = true)
  @Getter
  @Setter
  private KubernetesResourceDetails kubernetesResourceDetails;
}
