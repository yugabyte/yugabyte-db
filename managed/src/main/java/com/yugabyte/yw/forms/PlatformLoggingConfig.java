// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import javax.annotation.Nullable;
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
              + "Maximum size of the active application log file before it is rolled over,"
              + " for example \"1024MB\" or \"1GB\". Minimum 256MB.",
      example = "1024MB",
      required = false)
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.31.0.0")
  @Nullable
  private String maxFileSize;

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. "
              + "Total size of retained application log archives, for example \"20GB\". Archives"
              + " are deleted oldest first once this is exceeded, so on a high volume node this"
              + " binds before maxHistory and retains fewer days than maxHistory allows."
              + " Minimum 5GB.",
      example = "20GB",
      required = false)
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.31.0.0")
  @Nullable
  private String totalSizeCap;

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
