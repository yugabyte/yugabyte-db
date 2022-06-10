// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Objects;

@ApiModel(description = "SMTP configuration information")
public class SmtpData {
  @ApiModelProperty(value = "SMTP server", example = "smtp.example.com")
  public String smtpServer = null;

  @ApiModelProperty(value = "SMTP port number", example = "465")
  public int smtpPort = -1;

  @ApiModelProperty(value = "SMTP email 'from' address", example = "test@example.com")
  public String emailFrom = null;

  @ApiModelProperty(value = "SMTP email username", example = "testsmtp")
  public String smtpUsername = null;

  @ApiModelProperty(value = "SMTP password", example = "XurenRknsc")
  public String smtpPassword = null;

  @ApiModelProperty(value = "Connect to SMTP server using SSL", example = "true")
  public boolean useSSL = true;

  @ApiModelProperty(value = "Connect to SMTP server using TLS", example = "false")
  public boolean useTLS = false;

  @Override
  public int hashCode() {
    return Objects.hash(
        emailFrom, smtpPassword, smtpPort, smtpServer, smtpUsername, useSSL, useTLS);
  }

  @Override
  public boolean equals(Object obj) {
    if (this == obj) {
      return true;
    }
    if (!(obj instanceof SmtpData)) {
      return false;
    }
    SmtpData other = (SmtpData) obj;
    return Objects.equals(emailFrom, other.emailFrom)
        && Objects.equals(smtpPassword, other.smtpPassword)
        && smtpPort == other.smtpPort
        && Objects.equals(smtpServer, other.smtpServer)
        && Objects.equals(smtpUsername, other.smtpUsername)
        && useSSL == other.useSSL
        && useTLS == other.useTLS;
  }

  @Override
  public String toString() {
    return "SmtpData [smtpServer="
        + smtpServer
        + ", smtpPort="
        + smtpPort
        + ", emailFrom=REDACTED"
        + ", smtpUsername="
        + smtpUsername
        + ", useSSL="
        + useSSL
        + ", useTLS="
        + useTLS
        + "]";
  }
}
