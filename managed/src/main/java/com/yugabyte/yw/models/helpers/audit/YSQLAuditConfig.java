package com.yugabyte.yw.models.helpers.audit;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import javax.validation.constraints.NotNull;
import lombok.Data;

@Data
@ApiModel(description = "YSQL Audit Logging Configuration")
public class YSQLAuditConfig {

  @NotNull
  @ApiModelProperty(value = "Enabled", accessMode = READ_ONLY)
  private boolean enabled;

  @NotNull
  @ApiModelProperty(value = "YSQL statement classes", accessMode = READ_WRITE)
  private Set<YSQLAuditStatementClass> classes;

  @NotNull
  @ApiModelProperty(value = "Log catalog", accessMode = READ_WRITE)
  private boolean logCatalog;

  @NotNull
  @ApiModelProperty(value = "Log client", accessMode = READ_WRITE)
  private boolean logClient;

  @NotNull
  @ApiModelProperty(value = "Log level", accessMode = READ_WRITE)
  private YSQLAuditLogLevel logLevel;

  @NotNull
  @ApiModelProperty(value = "Log parameter", accessMode = READ_WRITE)
  private boolean logParameter;

  @NotNull
  @ApiModelProperty(value = "Log parameter max size", accessMode = READ_WRITE)
  private Integer logParameterMaxSize;

  @NotNull
  @ApiModelProperty(value = "Log relation", accessMode = READ_WRITE)
  private boolean logRelation;

  @NotNull
  @ApiModelProperty(value = "Log rows", accessMode = READ_WRITE)
  private boolean logRows;

  @NotNull
  @ApiModelProperty(value = "Log statement", accessMode = READ_WRITE)
  private boolean logStatement;

  @NotNull
  @ApiModelProperty(value = "Log statement once", accessMode = READ_WRITE)
  private boolean logStatementOnce;

  public enum YSQLAuditStatementClass {
    READ,
    WRITE,
    FUNCTION,
    ROLE,
    DDL,
    MISC,
    MISC_SET
  }

  public enum YSQLAuditLogLevel {
    DEBUG1,
    DEBUG2,
    DEBUG3,
    DEBUG4,
    DEBUG5,
    INFO,
    NOTICE,
    WARNING,
    LOG
  }
}
