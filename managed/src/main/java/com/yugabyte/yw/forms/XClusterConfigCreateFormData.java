package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import java.util.UUID;
import javax.validation.Valid;
import lombok.ToString;
import play.data.validation.Constraints.MaxLength;
import play.data.validation.Constraints.Pattern;
import play.data.validation.Constraints.Required;

@ApiModel(description = "xcluster create form")
@ToString
public class XClusterConfigCreateFormData {

  @Required
  @MaxLength(256)
  @ApiModelProperty(value = "Name", example = "Repl-config1", required = true)
  @Pattern(
      value = "^([^\\s_*<>?|\"\\x00]+)$",
      message =
          "The name of the replication config cannot contain "
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
      value = "Source Universe table IDs",
      example = "[\"000033df000030008000000000004006\", \"000033df00003000800000000000400b\"]",
      required = true)
  public Set<String> tables;

  @Valid
  @ApiModelProperty("Parameters needed for the bootstrap flow including backup/restore")
  public BootstrapParams bootstrapParams;

  @ApiModel(description = "Bootstrap parameters")
  @ToString
  public static class BootstrapParams {
    @Required
    @ApiModelProperty(
        value =
            "Source Universe table IDs that need bootstrapping; must be a subset of tables "
                + "in the main body",
        example = "[\"000033df000030008000000000004006\"]",
        required = true)
    public Set<String> tables;

    @Required
    @ApiModelProperty(value = "Parameters used to do Backup/restore", required = true)
    public BackupRequestParams backupRequestParams;
  }
}
