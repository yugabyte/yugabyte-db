// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.data;

import io.swagger.annotations.ApiModelProperty;
import javax.validation.constraints.Max;
import javax.validation.constraints.Min;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import play.data.validation.Constraints.Email;

public class CustomerConfigAlertsSmtpInfoData extends CustomerConfigAlertsData {
  @ApiModelProperty(value = "SMTP server", example = "smtp.example.com")
  @NotNull
  @Size(min = 1)
  public String smtpServer;

  @ApiModelProperty(value = "SMTP port number", example = "465")
  @Min(-1)
  @Max(65535)
  public int smtpPort = -1;

  @ApiModelProperty(value = "SMTP email 'from' address", example = "test@example.com")
  @Email
  public String emailFrom;

  @ApiModelProperty(value = "SMTP email username", example = "testsmtp")
  public String smtpUsername;

  @ApiModelProperty(value = "SMTP password", example = "XurenRknsc")
  public String smtpPassword;

  @ApiModelProperty(value = "Connect to SMTP server using SSL", example = "true")
  public boolean useSSL = true;

  @ApiModelProperty(value = "Connect to SMTP server using TLS", example = "false")
  public boolean useTLS = false;
}
