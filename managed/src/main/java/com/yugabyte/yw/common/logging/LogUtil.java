package com.yugabyte.yw.common.logging;

import ch.qos.logback.classic.LoggerContext;
import ch.qos.logback.classic.util.ContextInitializer;
import ch.qos.logback.core.joran.spi.JoranException;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class LogUtil {

  public static final Logger LOG = LoggerFactory.getLogger(LogUtil.class);
  private static final String LOGGING_CONFIG_KEY = "yb.logging.config";
  private static final String APPLICATION_LOG_LEVEL_SYS_PROPERTY = "applicationLogLevel";

  public static String getLoggingConfig(SettableRuntimeConfigFactory sConfigFactory) {
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
}
