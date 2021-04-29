// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;


import play.data.validation.Constraints;
import java.util.Map;
import java.util.UUID;


/**
 * This class will be used by the API and UI Form Elements to validate constraints are met
 */
public class CustomerRegisterFormData {
  @Constraints.Required()
  @Constraints.MaxLength(15)
  private String code;

  @Constraints.Required()
  @Constraints.Email
  private String email;

  private String password;

  private String confirmPassword;

  @Constraints.Required()
  @Constraints.MinLength(3)
  private String name;

  private Map features;

  public String getCode() {
    return code;
  }

  public void setCode(String code) {
    this.code = code;
  }

  public String getEmail() {
    return email;
  }

  public void setEmail(String email) {
    this.email = email;
  }

  public String getPassword() {
    return password;
  }

  public void setPassword(String password) {
    this.password = password;
  }

  public String getConfirmPassword() {
    return confirmPassword;
  }

  public void setConfirmPassword(String confirmPassword) {
    this.confirmPassword = confirmPassword;
  }

  public String getName() {
    return name;
  }

  public void setName(String name) {
    this.name = name;
  }

  public Map getFeatures() {
    return features;
  }

  public void setFeatures(Map features) {
    this.features = features;
  }

  public AlertingData getAlertingData() {
    return alertingData;
  }

  public void setAlertingData(AlertingData alertingData) {
    this.alertingData = alertingData;
  }

  public SmtpData getSmtpData() {
    return smtpData;
  }

  public void setSmtpData(SmtpData smtpData) {
    this.smtpData = smtpData;
  }

  public String getCallhomeLevel() {
    return callhomeLevel;
  }

  public void setCallhomeLevel(String callhomeLevel) {
    this.callhomeLevel = callhomeLevel;
  }

  static public class AlertingData {
    @Constraints.Email
    @Constraints.MinLength(5)
    public String alertingEmail;

    public boolean sendAlertsToYb = false;

    public long checkIntervalMs = 0;

    public long statusUpdateIntervalMs = 0;

    public Boolean reportOnlyErrors = false;

    public Boolean reportBackupFailures = false;
  }

  static public class SmtpData {
    public UUID configUUID = null;

    public String smtpServer = null;

    public int smtpPort = -1;

    public String emailFrom = null;

    public String smtpUsername = null;

    public String smtpPassword = null;

    public boolean useSSL = true;

    public boolean useTLS = false;

    @Override
    public int hashCode() {
      final int prime = 31;
      int result = 1;
      result = prime * result + ((configUUID == null) ? 0 : configUUID.hashCode());
      result = prime * result + ((emailFrom == null) ? 0 : emailFrom.hashCode());
      result = prime * result + ((smtpPassword == null) ? 0 : smtpPassword.hashCode());
      result = prime * result + smtpPort;
      result = prime * result + ((smtpServer == null) ? 0 : smtpServer.hashCode());
      result = prime * result + ((smtpUsername == null) ? 0 : smtpUsername.hashCode());
      result = prime * result + (useSSL ? 1231 : 1237);
      result = prime * result + (useTLS ? 1231 : 1237);
      return result;
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
      if (configUUID == null) {
        if (other.configUUID != null) {
          return false;
        }
      } else if (!configUUID.equals(other.configUUID)) {
        return false;
      }
      if (emailFrom == null) {
        if (other.emailFrom != null) {
          return false;
        }
      } else if (!emailFrom.equals(other.emailFrom)) {
        return false;
      }
      if (smtpPassword == null) {
        if (other.smtpPassword != null) {
          return false;
        }
      } else if (!smtpPassword.equals(other.smtpPassword)) {
        return false;
      }
      if (smtpPort != other.smtpPort) {
        return false;
      }
      if (smtpServer == null) {
        if (other.smtpServer != null) {
          return false;
        }
      } else if (!smtpServer.equals(other.smtpServer)) {
        return false;
      }
      if (smtpUsername == null) {
        if (other.smtpUsername != null) {
          return false;
        }
      } else if (!smtpUsername.equals(other.smtpUsername)) {
        return false;
      }
      if (useSSL != other.useSSL) {
        return false;
      }
      if (useTLS != other.useTLS) {
        return false;
      }
      return true;
    }
  }

  public AlertingData alertingData;

  public SmtpData smtpData;

  @Constraints.Pattern(message="Must be one of NONE, LOW, MEDIUM, HIGH", value="\\b(?:NONE|LOW|MEDIUM|HIGH)\\b")
  public String callhomeLevel = "MEDIUM";
}
