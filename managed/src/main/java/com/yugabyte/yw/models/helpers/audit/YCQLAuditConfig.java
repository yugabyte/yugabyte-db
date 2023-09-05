package com.yugabyte.yw.models.helpers.audit;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import javax.validation.constraints.NotNull;
import lombok.Data;

@Data
@ApiModel(description = "YCQL Audit Logging Configuration")
public class YCQLAuditConfig {

  @NotNull
  @ApiModelProperty(value = "Enabled", accessMode = READ_WRITE)
  private boolean enabled;

  @NotNull
  @ApiModelProperty(value = "Included categories", accessMode = READ_WRITE)
  private Set<YCQLAuditCategory> includedCategories;

  @NotNull
  @ApiModelProperty(value = "Excluded Categories", accessMode = READ_WRITE)
  private Set<YCQLAuditCategory> excludedCategories;

  @NotNull
  @ApiModelProperty(value = "Included Users", accessMode = READ_WRITE)
  private Set<String> includedUsers;

  @NotNull
  @ApiModelProperty(value = "Excluded Users", accessMode = READ_WRITE)
  private Set<String> excludedUsers;

  @NotNull
  @ApiModelProperty(value = "Included Keyspaces", accessMode = READ_WRITE)
  private Set<String> includedKeyspaces;

  @NotNull
  @ApiModelProperty(value = "Excluded Keyspaces", accessMode = READ_WRITE)
  private Set<String> excludedKeyspaces;

  @NotNull
  @ApiModelProperty(value = "Log Level", accessMode = READ_WRITE)
  private YCQLAuditLogLevel logLevel;

  public enum YCQLAuditCategory {
    QUERY,
    DML,
    DDL,
    DCL,
    AUTH,
    PREPARE,
    ERROR,
    OTHER
  }

  public enum YCQLAuditLogLevel {
    INFO,
    WARNING,
    ERROR
  }
}
