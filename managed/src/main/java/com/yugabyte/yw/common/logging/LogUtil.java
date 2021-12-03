package com.yugabyte.yw.common.logging;

import ch.qos.logback.classic.LoggerContext;
import ch.qos.logback.classic.util.ContextInitializer;
import ch.qos.logback.core.joran.spi.JoranException;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class LogUtil {

  public static final Logger LOG = LoggerFactory.getLogger(LogUtil.class);
  private static final String LOGGING_CONFIG_KEY = "yb.logging.config";
  private static final String APPLICATION_LOG_LEVEL_SYS_PROPERTY = "applicationLogLevel";

  private static final String LOG_OVERRIDE_PATH_SYS_PROPERTY = "log.override.path";
  private static final String YB_CLOUD_ENABLED_SYS_PROPERTY = "yb.cloud.enabled";
  private static final String APPLICATION_HOME_SYS_PROPERTY = "application.home";
  private static final String[] fixedLogbackSysProperties = {
    LOG_OVERRIDE_PATH_SYS_PROPERTY, YB_CLOUD_ENABLED_SYS_PROPERTY, APPLICATION_HOME_SYS_PROPERTY
  };

  public static void updateLoggingFromConfig(
      SettableRuntimeConfigFactory sConfigFactory, Config config) {
    String logLevel = LogUtil.getLoggingLevel(sConfigFactory);
    // need to configure this to simulate the effect of play.logger.includeConfigProperties=true
    configureFixedLogbackSystemProperties(config);
    try {
      setLoggingLevel(logLevel);
    } catch (JoranException ex) {
      LOG.warn("Could not initialize logback");
    }
  }

  private static String getLoggingLevel(SettableRuntimeConfigFactory sConfigFactory) {
    return sConfigFactory.globalRuntimeConf().getString(LOGGING_CONFIG_KEY);
  }

  public static void setLoggingConfig(SettableRuntimeConfigFactory sConfigFactory, String level) {
    sConfigFactory.globalRuntimeConf().setValue(LOGGING_CONFIG_KEY, level);
  }

  /**
   * Updates system property to new log level and refresh logback.xml to read in the new system
   * property.
   *
   * @param level The new log level to be set
   */
  public static void setLoggingLevel(String level) throws JoranException {
    System.setProperty(APPLICATION_LOG_LEVEL_SYS_PROPERTY, level);
    LoggerContext loggerContext = (LoggerContext) LoggerFactory.getILoggerFactory();
    loggerContext.reset();
    ContextInitializer ci = new ContextInitializer(loggerContext);
    ci.autoConfig();
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
