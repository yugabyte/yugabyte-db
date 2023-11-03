package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import java.util.UUID;
import javax.validation.Valid;
import lombok.AllArgsConstructor;
import lombok.NoArgsConstructor;
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
  @ApiModelProperty("Parameters used to do Backup/restore")
  public XClusterConfigCreateFormData.BootstrapParams.BootstarpBackupParams bootstrapBackupParams;

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

    @ApiModelProperty(value = "Time interval between snapshots in seconds")
    public long snapshotIntervalSec = 86400L;
  }
}
