// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import java.util.Objects;

public class SmtpData {
  public String smtpServer = null;

  public int smtpPort = -1;

  public String emailFrom = null;

  public String smtpUsername = null;

  public String smtpPassword = null;

  public boolean useSSL = true;

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
        + ", emailFrom="
        + emailFrom
        + ", smtpUsername="
        + smtpUsername
        + ", useSSL="
        + useSSL
        + ", useTLS="
        + useTLS
        + "]";
  }
}
