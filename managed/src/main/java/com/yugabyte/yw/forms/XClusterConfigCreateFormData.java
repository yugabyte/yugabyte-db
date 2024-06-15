package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.common.YbaApi;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import java.util.UUID;
import javax.validation.Valid;
import lombok.ToString;
import play.data.validation.Constraints;
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

  @ApiModelProperty(value = "configType", allowableValues = "Basic, Txn")
  public XClusterConfig.ConfigType configType = ConfigType.Basic;

  @ApiModelProperty("Run the pre-checks without actually running the subtasks")
  public boolean dryRun = false;

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
    public BootstarpBackupParams backupRequestParams;

    @ApiModelProperty(
        value =
            "WARNING: This is a preview API that could change. Allow backup on whole database when"
                + " only set of tables require bootstrap",
        required = false)
    @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.23.0.0")
    public boolean allowBootstrap = false;

    @ApiModel(description = "Backup parameters for bootstrapping")
    @ToString
    public static class BootstarpBackupParams {
      @Constraints.Required
      @ApiModelProperty(value = "Storage configuration UUID", required = true)
      public UUID storageConfigUUID;

      // The number of concurrent commands to run on nodes over SSH
      @ApiModelProperty(
          value =
              "Number of concurrent commands used by yb_backup (not ybc) to run on nodes over SSH")
      public int parallelism = 8;
    }
  }
}
