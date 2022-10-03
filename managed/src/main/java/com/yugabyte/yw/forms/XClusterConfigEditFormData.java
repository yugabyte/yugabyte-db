package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import javax.validation.Valid;
import lombok.ToString;
import play.data.validation.Constraints;
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

  @Pattern("^(Running|Paused)$")
  @ApiModelProperty(value = "Status", allowableValues = "Running, Paused")
  public String status;

  @ApiModelProperty(
      value = "Source universe table IDs",
      example = "[\"000033df000030008000000000004006\", \"000033df00003000800000000000400b\"]")
  public Set<String> tables;

  @Valid
  @ApiModelProperty("Parameters needed for the bootstrap flow including backup/restore")
  public XClusterConfigCreateFormData.BootstrapParams bootstrapParams;
}
