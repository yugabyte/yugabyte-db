package com.yugabyte.yw.common.logging;

import ch.qos.logback.classic.LoggerContext;
import ch.qos.logback.classic.util.ContextInitializer;
import ch.qos.logback.core.joran.spi.JoranException;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.AuditLoggingConfig;
import javax.annotation.Nullable;
import javax.validation.constraints.NotNull;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class LogUtil {

  public static final Logger LOG = LoggerFactory.getLogger(LogUtil.class);
  private static final String LOGGING_CONFIG_KEY = "yb.logging.config";
  private static final String LOGGING_ROLLOVER_PATTERN_KEY = "yb.logging.rollover_pattern";
  private static final String LOGGING_MAX_HISTORY_KEY = "yb.logging.max_history";
  private static final String APPLICATION_LOG_LEVEL_SYS_PROPERTY = "applicationLogLevel";
  private static final String LOG_ROLLOVER_PATTERN_SYS_PROPERTY = "applicationLogRolloverPattern";
  private static final String LOG_MAX_HISTORY_SYS_PROPERTY = "applicationLogMaxHistory";
  private static final String LOGGING_FILE_PREFIX_KEY = "yb.logging.fileNamePrefix";
  private static final String APPLICATION_LOG_FILE_PREFIX_SYS_PROPERTY =
      "applicationLogFileNamePrefix";

  private static final String LOG_OVERRIDE_PATH_SYS_PROPERTY = "log.override.path";
  private static final String YB_CLOUD_ENABLED_SYS_PROPERTY = "yb.cloud.enabled";
  private static final String APPLICATION_HOME_SYS_PROPERTY = "application.home";
  private static final String[] fixedLogbackSysProperties = {
    LOG_OVERRIDE_PATH_SYS_PROPERTY, YB_CLOUD_ENABLED_SYS_PROPERTY, APPLICATION_HOME_SYS_PROPERTY
  };

  private static final String AUDIT_LOG_OUTPUT_TO_STDOUT_KEY = "yb.audit.log.outputToStdout";
  private static final String AUDIT_LOG_OUTPUT_TO_STDOUT_SYS_PROPERTY = "auditLogOutputToStdout";
  private static final String AUDIT_LOG_OUTPUT_TO_FILE_KEY = "yb.audit.log.outputToFile";
  private static final String AUDIT_LOG_OUTPUT_TO_FILE_SYS_PROPERTY = "auditLogOutputToFile";
  private static final String AUDIT_LOG_ROLLOVER_PATTERN_KEY = "yb.audit.log.rolloverPattern";
  private static final String AUDIT_LOG_ROLLOVER_PATTERN_SYS_PROPERTY = "auditLogRolloverPattern";
  private static final String AUDIT_LOG_MAX_HISTORY_KEY = "yb.audit.log.maxHistory";
  private static final String AUDIT_LOG_MAX_HISTORY_SYS_PROPERTY = "auditLogMaxHistory";
  private static final String AUDIT_LOG_FILE_PREFIX_KEY = "yb.audit.log.fileNamePrefix";
  private static final String AUDIT_LOG_FILE_PREFIX_SYS_PROPERTY = "auditLogFileNamePrefix";

  /*[PLAT-3932]: Key for storing the Correlation ID of HTTP requests in the MDC for logging
   * purposes.
   * */
  public static final String CORRELATION_ID = "correlation-id";

  public static void updateLoggingFromConfig(
      SettableRuntimeConfigFactory sConfigFactory, Config config) {
    String logLevel = LogUtil.getLoggingLevel(sConfigFactory);
    String rolloverPattern = LogUtil.getLoggingRolloverPattern(sConfigFactory);
    Integer maxHistory = LogUtil.getLoggingMaxHistory(sConfigFactory);
    // need to configure this to simulate the effect of play.logger.includeConfigProperties=true
    configureFixedLogbackSystemProperties(config);
    try {
      updateApplicationLoggingContext(logLevel, rolloverPattern, maxHistory, "");
    } catch (JoranException ex) {
      LOG.warn("Could not initialize logback");
    }
  }

  public static void updateAuditLoggingFromConfig(
      SettableRuntimeConfigFactory sConfigFactory, Config config) {
    Boolean auditLogOutputToStdout = LogUtil.getAuditLoggingOutputToStdout(sConfigFactory);
    Boolean auditLogOutputToFile = LogUtil.getAuditLoggingOutputToFile(sConfigFactory);
    String auditLogRolloverPattern = LogUtil.getAuditLoggingRolloverPattern(sConfigFactory);
    Integer auditLogMaxHistory = LogUtil.getAuditLoggingMaxHistory(sConfigFactory);
    configureFixedLogbackSystemProperties(config);
    try {
      updateAuditLoggingContext(
          auditLogOutputToStdout,
          auditLogOutputToFile,
          auditLogRolloverPattern,
          auditLogMaxHistory,
          "");
    } catch (JoranException ex) {
      LOG.warn("Could not initialize logback");
    }
  }

  private static String getLoggingLevel(SettableRuntimeConfigFactory sConfigFactory) {
    return sConfigFactory.globalRuntimeConf().getString(LOGGING_CONFIG_KEY);
  }

  private static String getLoggingRolloverPattern(SettableRuntimeConfigFactory sConfigFactory) {
    return sConfigFactory.globalRuntimeConf().getString(LOGGING_ROLLOVER_PATTERN_KEY);
  }

  private static Integer getLoggingMaxHistory(SettableRuntimeConfigFactory sConfigFactory) {
    String maxHistoryStr = sConfigFactory.globalRuntimeConf().getString(LOGGING_MAX_HISTORY_KEY);
    return StringUtils.isEmpty(maxHistoryStr) ? null : Integer.valueOf(maxHistoryStr);
  }

  private static Boolean getAuditLoggingOutputToStdout(
      SettableRuntimeConfigFactory sConfigFactory) {
    return sConfigFactory.globalRuntimeConf().getBoolean(AUDIT_LOG_OUTPUT_TO_STDOUT_KEY);
  }

  private static Boolean getAuditLoggingOutputToFile(SettableRuntimeConfigFactory sConfigFactory) {
    return sConfigFactory.globalRuntimeConf().getBoolean(AUDIT_LOG_OUTPUT_TO_FILE_KEY);
  }

  private static String getAuditLoggingRolloverPattern(
      SettableRuntimeConfigFactory sConfigFactory) {
    return sConfigFactory.globalRuntimeConf().getString(AUDIT_LOG_ROLLOVER_PATTERN_KEY);
  }

  private static Integer getAuditLoggingMaxHistory(SettableRuntimeConfigFactory sConfigFactory) {
    return sConfigFactory.globalRuntimeConf().getInt(AUDIT_LOG_MAX_HISTORY_KEY);
  }

  public static void updateApplicationLoggingConfig(
      SettableRuntimeConfigFactory sConfigFactory,
      @NotNull String level,
      @Nullable String rolloverPattern,
      @Nullable Integer maxHistory,
      @Nullable String applicationLogFileNamePrefix) {
    sConfigFactory.globalRuntimeConf().setValue(LOGGING_CONFIG_KEY, level);
    if (rolloverPattern != null) {
      sConfigFactory.globalRuntimeConf().setValue(LOGGING_ROLLOVER_PATTERN_KEY, rolloverPattern);
    }
    if (maxHistory != null) {
      sConfigFactory
          .globalRuntimeConf()
          .setValue(LOGGING_MAX_HISTORY_KEY, String.valueOf(maxHistory));
    }
    if (applicationLogFileNamePrefix != null) {
      sConfigFactory
          .globalRuntimeConf()
          .setValue(LOGGING_FILE_PREFIX_KEY, applicationLogFileNamePrefix);
    }
  }

  public static void updateAuditLoggingConfig(
      SettableRuntimeConfigFactory sConfigFactory, AuditLoggingConfig config) {

    boolean outputToStdout = config.isOutputToStdout();
    boolean outputToFile = config.isOutputToFile();
    String rolloverPattern = config.getRolloverPattern();
    int maxHistory = config.getMaxHistory();
    String auditLogFileNamePrefix = config.getFileNamePrefix();

    sConfigFactory
        .globalRuntimeConf()
        .setValue(AUDIT_LOG_OUTPUT_TO_STDOUT_KEY, String.valueOf(outputToStdout));
    sConfigFactory
        .globalRuntimeConf()
        .setValue(AUDIT_LOG_OUTPUT_TO_FILE_KEY, String.valueOf(outputToFile));
    sConfigFactory.globalRuntimeConf().setValue(AUDIT_LOG_ROLLOVER_PATTERN_KEY, rolloverPattern);
    sConfigFactory
        .globalRuntimeConf()
        .setValue(AUDIT_LOG_MAX_HISTORY_KEY, String.valueOf(maxHistory));
    if (auditLogFileNamePrefix != null) {
      sConfigFactory
          .globalRuntimeConf()
          .setValue(AUDIT_LOG_FILE_PREFIX_KEY, auditLogFileNamePrefix);
    }
  }

  /**
   * Updates system property to new log level/rollover pattern/max history and refresh logback.xml
   * to read in the new system property.
   *
   * @param level The new log level to be set
   */
  public static void updateApplicationLoggingContext(
      @NotNull String level,
      @Nullable String rolloverPattern,
      @Nullable Integer maxHistory,
      @Nullable String applicationLogFileNamePrefix)
      throws JoranException {
    String curLevel = System.getProperty(APPLICATION_LOG_LEVEL_SYS_PROPERTY);
    String curRolloverPattern = System.getProperty(LOG_ROLLOVER_PATTERN_SYS_PROPERTY);
    String curMaxHistoryStr = System.getProperty(LOG_MAX_HISTORY_SYS_PROPERTY);
    String curApplicationLogFileNamePrefix =
        System.getProperty(APPLICATION_LOG_FILE_PREFIX_SYS_PROPERTY);
    setApplicationLoggingSystemProperties(
        level, rolloverPattern, maxHistory, applicationLogFileNamePrefix, false);
    LOG.debug(
        "Update logging context: '{}' '{}' '{}' '{}' (current '{}' '{}' '{}' '{}')",
        level,
        rolloverPattern,
        maxHistory,
        applicationLogFileNamePrefix,
        curLevel,
        curRolloverPattern,
        curMaxHistoryStr,
        curApplicationLogFileNamePrefix);
    try {
      restartLogback();
    } catch (JoranException e) {
      // rollback to prev state
      setApplicationLoggingSystemProperties(
          curLevel,
          curRolloverPattern,
          curMaxHistoryStr == null ? null : Integer.valueOf(curMaxHistoryStr),
          curApplicationLogFileNamePrefix,
          true);
      restartLogback();
      throw e;
    }
  }

  public static void updateAuditLoggingContext(AuditLoggingConfig config) throws JoranException {
    boolean outputToStdout = config.isOutputToStdout();
    boolean outputToFile = config.isOutputToFile();
    String rolloverPattern = config.getRolloverPattern();
    int maxHistory = config.getMaxHistory();
    String auditLogFileNamePrefix = config.getFileNamePrefix();
    updateAuditLoggingContext(
        outputToStdout, outputToFile, rolloverPattern, maxHistory, auditLogFileNamePrefix);
  }

  public static void updateAuditLoggingContext(
      boolean outputToStdout,
      boolean outputToFile,
      String rolloverPattern,
      int maxHistory,
      String auditLogFileNamePrefix)
      throws JoranException {

    String curOutputToStdout = System.getProperty(AUDIT_LOG_OUTPUT_TO_STDOUT_SYS_PROPERTY);
    String curOutputToFile = System.getProperty(AUDIT_LOG_OUTPUT_TO_FILE_SYS_PROPERTY);
    String curRolloverPattern = System.getProperty(AUDIT_LOG_ROLLOVER_PATTERN_SYS_PROPERTY);
    String curMaxHistoryStr = System.getProperty(AUDIT_LOG_MAX_HISTORY_SYS_PROPERTY);
    String curAuditLogFileNamePrefix = System.getProperty(AUDIT_LOG_FILE_PREFIX_SYS_PROPERTY);
    setAuditLoggingSystemProperties(
        outputToStdout, outputToFile, rolloverPattern, maxHistory, auditLogFileNamePrefix, false);
    LOG.debug(
        "Update logging context: '{}' '{}' '{}' '{}' '{}' (current '{}' '{}' '{}' '{}' '{}')",
        outputToStdout,
        outputToFile,
        rolloverPattern,
        maxHistory,
        auditLogFileNamePrefix,
        curOutputToStdout,
        curOutputToFile,
        curRolloverPattern,
        curMaxHistoryStr,
        curAuditLogFileNamePrefix);
    try {
      restartLogback();
    } catch (JoranException e) {
      // rollback to prev state
      setAuditLoggingSystemProperties(
          curOutputToStdout == null ? null : Boolean.valueOf(curOutputToStdout),
          curOutputToFile == null ? null : Boolean.valueOf(curOutputToFile),
          curRolloverPattern,
          curMaxHistoryStr == null ? null : Integer.valueOf(curMaxHistoryStr),
          auditLogFileNamePrefix,
          true);
      restartLogback();
      throw e;
    }
  }

  private static void restartLogback() throws JoranException {
    LoggerContext loggerContext = (LoggerContext) LoggerFactory.getILoggerFactory();
    loggerContext.reset();
    ContextInitializer ci = new ContextInitializer(loggerContext);
    ci.autoConfig();
  }

  private static void setApplicationLoggingSystemProperties(
      @NotNull String level,
      @Nullable String rolloverPattern,
      @Nullable Integer maxHistory,
      @Nullable String applicationLogFileNamePrefix,
      boolean eraseIfEmpty) {
    setSystemProperty(APPLICATION_LOG_LEVEL_SYS_PROPERTY, level, eraseIfEmpty);
    setSystemProperty(LOG_ROLLOVER_PATTERN_SYS_PROPERTY, rolloverPattern, eraseIfEmpty);
    setSystemProperty(
        LOG_MAX_HISTORY_SYS_PROPERTY,
        maxHistory == null ? null : String.valueOf(maxHistory),
        eraseIfEmpty);
    setSystemProperty(
        APPLICATION_LOG_FILE_PREFIX_SYS_PROPERTY, applicationLogFileNamePrefix, eraseIfEmpty);
  }

  private static void setAuditLoggingSystemProperties(
      Boolean outputToStdout,
      Boolean outputToFile,
      String rolloverPattern,
      Integer maxHistory,
      String auditLogFileNamePrefix,
      boolean eraseIfEmpty) {
    setSystemProperty(
        AUDIT_LOG_OUTPUT_TO_STDOUT_SYS_PROPERTY,
        outputToStdout == null ? null : String.valueOf(outputToStdout),
        eraseIfEmpty);
    setSystemProperty(
        AUDIT_LOG_OUTPUT_TO_FILE_SYS_PROPERTY,
        outputToFile == null ? null : String.valueOf(outputToFile),
        eraseIfEmpty);
    setSystemProperty(AUDIT_LOG_ROLLOVER_PATTERN_SYS_PROPERTY, rolloverPattern, eraseIfEmpty);
    setSystemProperty(
        AUDIT_LOG_MAX_HISTORY_SYS_PROPERTY,
        maxHistory == null ? null : String.valueOf(maxHistory),
        eraseIfEmpty);
    setSystemProperty(AUDIT_LOG_FILE_PREFIX_SYS_PROPERTY, auditLogFileNamePrefix, eraseIfEmpty);
  }

  public static void configureFixedLogbackSystemProperties(Config config) {
    for (String sysPropertyName : fixedLogbackSysProperties) {
      boolean hasPath = config.hasPath(sysPropertyName);
      if (hasPath) {
        String value = config.getString(sysPropertyName);
        setSystemProperty(sysPropertyName, value, false);
      }
    }
  }

  private static void setSystemProperty(String key, String value, boolean eraseIfEmpty) {
    if (value != null) {
      System.setProperty(key, value);
    } else if (eraseIfEmpty) {
      System.clearProperty(key);
    }
  }
}
