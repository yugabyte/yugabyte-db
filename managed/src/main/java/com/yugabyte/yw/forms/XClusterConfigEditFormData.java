package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import javax.validation.Valid;
import org.yb.cdc.CdcConsumer.XClusterRole;
import play.data.validation.Constraints.MaxLength;
import play.data.validation.Constraints.Pattern;

@ApiModel(description = "xcluster edit form")
public class XClusterConfigEditFormData {

  @MaxLength(256)
  @ApiModelProperty(value = "Name", example = "Repl-config1")
  @Pattern(
      value = "^([^\\s_*<>?|\"\\x00]+)$",
      message =
          "The name of the replication config cannot contain "
              + "[SPACE '_' '*' '<' '>' '?' '|' '\"' NULL] characters")
  public String name;

  @Pattern(
      value = "^(Running|Paused)$",
      message = "status can be set either to `Running` or `Paused`")
  @ApiModelProperty(value = "Status", allowableValues = "Running, Paused")
  public String status;

  @ApiModelProperty(
      value = "Source universe table IDs",
      example = "[\"000033df000030008000000000004006\", \"000033df00003000800000000000400b\"]")
  public Set<String> tables;

  @ApiModelProperty(
      value =
          "Whether or not YBA should also include all index tables from any provided main tables.")
  public boolean autoIncludeIndexTables = true;

  @Valid
  @ApiModelProperty("Parameters needed for the bootstrap flow including backup/restore")
  public XClusterConfigCreateFormData.BootstrapParams bootstrapParams;

  @ApiModelProperty("Run the pre-checks without actually running the subtasks")
  public boolean dryRun = false;

  @ApiModelProperty("The role that the source universe should have in the xCluster config")
  public XClusterRole sourceRole;

  @ApiModelProperty("The role that the target universe should have in the xCluster config")
  public XClusterRole targetRole;
}
