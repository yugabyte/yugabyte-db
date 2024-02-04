// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModelProperty;
import javax.annotation.Nullable;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.validation.constraints.Min;
import javax.validation.constraints.NotNull;
import lombok.Data;
import org.slf4j.event.Level;

@Data
public class PlatformLoggingConfig {

  @NotNull
  @Enumerated(EnumType.STRING)
  private Level level;

  @Nullable private String rolloverPattern;

  @Nullable
  @Min(value = 0)
  private Integer maxHistory;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. "
              + "Application log file name prefix. Defaults to \"\". For example, setting this to"
              + " \"yb-platform-\" will generate application log files as"
              + " \"yb-platform-application.log\" instead of \"application.log\".",
      example = "yb-platform-",
      required = false)
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.21.0.0")
  private String fileNamePrefix;
}
