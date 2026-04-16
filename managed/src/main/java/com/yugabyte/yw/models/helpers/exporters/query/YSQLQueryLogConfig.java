package com.yugabyte.yw.models.helpers.exporters.query;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import javax.validation.constraints.NotNull;
import lombok.Data;

@Data
@ApiModel(description = "YSQL Query Logging Configuration")
public class YSQLQueryLogConfig {

  @NotNull
  @ApiModelProperty(value = "Enabled", accessMode = READ_ONLY)
  private boolean enabled;

  @NotNull
  @ApiModelProperty(
      value = "Log statement, controls which SQL statements are logged.",
      accessMode = READ_WRITE)
  private YSQLLogStatement logStatement = YSQLLogStatement.NONE;

  @NotNull
  @ApiModelProperty(
      value =
          "Log min error statement, controls which SQL statements that cause an error condition are"
              + " recorded.",
      accessMode = READ_WRITE)
  private YSQlLogMinErrorStatement logMinErrorStatement = YSQlLogMinErrorStatement.ERROR;

  @NotNull
  @ApiModelProperty(
      value =
          "Log error verbosity, controls the amount of detail written in the server log for each"
              + " message that is logged.",
      accessMode = READ_WRITE)
  private YSQLLogErrorVerbosity logErrorVerbosity = YSQLLogErrorVerbosity.DEFAULT;

  @NotNull
  @ApiModelProperty(
      value = "Log duration, causes the duration of every completed statement to be logged",
      accessMode = READ_WRITE)
  private boolean logDuration = false;

  @NotNull
  @ApiModelProperty(
      value =
          "Debug print plan, these parameters enable various debugging output to be emitted. When"
              + " set, they print the resulting parse tree, the query rewriter output, or the"
              + " execution plan for each executed query.",
      accessMode = READ_WRITE)
  private boolean debugPrintPlan = false;

  @NotNull
  @ApiModelProperty(
      value =
          "Log connections, causes each attempted connection to the server to be logged, as well as"
              + " successful completion of both client authentication (if necessary) and"
              + " authorization",
      accessMode = READ_WRITE)
  private boolean logConnections = false;

  @NotNull
  @ApiModelProperty(
      value =
          "Log disconnections, causes session terminations to be logged. The log output provides"
              + " information similar to log_connections, plus the duration of the session.",
      accessMode = READ_WRITE)
  private boolean logDisconnections = false;

  @NotNull
  @ApiModelProperty(
      value =
          "Log min duration statement, causes the duration of each completed statement to be logged"
              + " if the statement ran for at least the specified amount of time.",
      accessMode = READ_WRITE)
  private Integer logMinDurationStatement = -1;

  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2025.2.0.0")
  @ApiModelProperty(
      value =
          "YbaApi Internal. Log line prefix format for PostgreSQL logs. User-configured"
              + " log_line_prefix in gflags takes precedence over this value when specified.",
      accessMode = READ_WRITE)
  private String logLinePrefix;

  public enum YSQlLogMinErrorStatement {
    ERROR
  }

  public enum YSQLLogStatement {
    ALL,
    NONE,
    DDL,
    MOD
  }

  public enum YSQLLogErrorVerbosity {
    VERBOSE,
    TERSE,
    DEFAULT
  }
}
