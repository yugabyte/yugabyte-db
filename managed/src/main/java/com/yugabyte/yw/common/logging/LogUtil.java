package com.yugabyte.yw.common.logging;

import ch.qos.logback.classic.LoggerContext;
import ch.qos.logback.classic.util.ContextInitializer;
import ch.qos.logback.core.joran.spi.JoranException;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
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
  private static final String LOG_ROLLOVER_PATTERN_SYS_PROPERTY = "logRolloverPattern";
  private static final String LOG_MAX_HISTORY_SYS_PROPERTY = "logMaxHistory";

  private static final String LOG_OVERRIDE_PATH_SYS_PROPERTY = "log.override.path";
  private static final String YB_CLOUD_ENABLED_SYS_PROPERTY = "yb.cloud.enabled";
  private static final String APPLICATION_HOME_SYS_PROPERTY = "application.home";
  private static final String[] fixedLogbackSysProperties = {
    LOG_OVERRIDE_PATH_SYS_PROPERTY, YB_CLOUD_ENABLED_SYS_PROPERTY, APPLICATION_HOME_SYS_PROPERTY
  };

  public static void updateLoggingFromConfig(
      SettableRuntimeConfigFactory sConfigFactory, Config config) {
    String logLevel = LogUtil.getLoggingLevel(sConfigFactory);
    String rolloverPattern = LogUtil.getLoggingRolloverPattern(sConfigFactory);
    Integer maxHistory = LogUtil.getLoggingMaxHistory(sConfigFactory);
    // need to configure this to simulate the effect of play.logger.includeConfigProperties=true
    configureFixedLogbackSystemProperties(config);
    try {
      updateLoggingContext(logLevel, rolloverPattern, maxHistory);
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

  public static void updateLoggingConfig(
      SettableRuntimeConfigFactory sConfigFactory,
      @NotNull String level,
      @Nullable String rolloverPattern,
      @Nullable Integer maxHistory) {
    sConfigFactory.globalRuntimeConf().setValue(LOGGING_CONFIG_KEY, level);
    if (rolloverPattern != null) {
      sConfigFactory.globalRuntimeConf().setValue(LOGGING_ROLLOVER_PATTERN_KEY, rolloverPattern);
    }
    if (maxHistory != null) {
      sConfigFactory
          .globalRuntimeConf()
          .setValue(LOGGING_MAX_HISTORY_KEY, String.valueOf(maxHistory));
    }
  }

  /**
   * Updates system property to new log level/rollover pattern/max history and refresh logback.xml
   * to read in the new system property.
   *
   * @param level The new log level to be set
   */
  public static void updateLoggingContext(
      @NotNull String level, @Nullable String rolloverPattern, @Nullable Integer maxHistory)
      throws JoranException {
    String curLevel = System.getProperty(APPLICATION_LOG_LEVEL_SYS_PROPERTY);
    String curRolloverPattern = System.getProperty(LOG_ROLLOVER_PATTERN_SYS_PROPERTY);
    String curMaxHistoryStr = System.getProperty(LOG_MAX_HISTORY_SYS_PROPERTY);
    setLoggingSystemProperties(level, rolloverPattern, maxHistory, false);
    LOG.debug(
        "Update logging context: '{}' '{}' '{}' (current '{}' '{}' '{}')",
        level,
        rolloverPattern,
        maxHistory,
        curLevel,
        curRolloverPattern,
        curMaxHistoryStr);
    try {
      restartLogback();
    } catch (JoranException e) {
      // rollback to prev state
      setLoggingSystemProperties(
          curLevel,
          curRolloverPattern,
          curMaxHistoryStr == null ? null : Integer.valueOf(curMaxHistoryStr),
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

  private static void setLoggingSystemProperties(
      @NotNull String level,
      @Nullable String rolloverPattern,
      @Nullable Integer maxHistory,
      boolean eraseIfEmpty) {
    setSystemProperty(APPLICATION_LOG_LEVEL_SYS_PROPERTY, level, eraseIfEmpty);
    setSystemProperty(LOG_ROLLOVER_PATTERN_SYS_PROPERTY, rolloverPattern, eraseIfEmpty);
    setSystemProperty(
        LOG_MAX_HISTORY_SYS_PROPERTY,
        maxHistory == null ? null : String.valueOf(maxHistory),
        eraseIfEmpty);
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
