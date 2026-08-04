package com.yugabyte.yw.models.helpers.exporters.audit;

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

  @ApiModelProperty(value = "Included categories", accessMode = READ_WRITE)
  private Set<YCQLAuditCategory> includedCategories;

  @ApiModelProperty(value = "Excluded Categories", accessMode = READ_WRITE)
  private Set<YCQLAuditCategory> excludedCategories;

  @ApiModelProperty(value = "Included Users", accessMode = READ_WRITE)
  private Set<String> includedUsers;

  @ApiModelProperty(value = "Excluded Users", accessMode = READ_WRITE)
  private Set<String> excludedUsers;

  @ApiModelProperty(value = "Included Keyspaces", accessMode = READ_WRITE)
  private Set<String> includedKeyspaces;

  @ApiModelProperty(value = "Excluded Keyspaces", accessMode = READ_WRITE)
  private Set<String> excludedKeyspaces;

  @ApiModelProperty(value = "Log Level", accessMode = READ_WRITE)
  private YCQLAuditLogLevel logLevel;

  @ApiModelProperty(
      value =
          "Number of days to keep gzipped YCQL audit log archives on the node."
              + " 0 or unset disables the dedicated audit-log retention pipeline and keeps the"
              + " default size-based tserver log purge behavior.",
      accessMode = READ_WRITE)
  private Integer logRetentionDays;

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
