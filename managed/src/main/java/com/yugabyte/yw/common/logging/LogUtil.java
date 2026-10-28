package com.yugabyte.yw.common.logging;

import ch.qos.logback.classic.LoggerContext;
import ch.qos.logback.classic.util.ContextInitializer;
import ch.qos.logback.core.joran.spi.JoranException;
import ch.qos.logback.core.util.FileSize;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.AuditLoggingConfig;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
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
  private static final String LOGGING_MAX_FILE_SIZE_KEY = "yb.logging.max_file_size";
  private static final String LOGGING_TOTAL_SIZE_CAP_KEY = "yb.logging.total_size_cap";
  private static final String APPLICATION_LOG_LEVEL_SYS_PROPERTY = "applicationLogLevel";
  private static final String LOG_ROLLOVER_PATTERN_SYS_PROPERTY = "applicationLogRolloverPattern";
  private static final String LOG_MAX_HISTORY_SYS_PROPERTY = "applicationLogMaxHistory";
  private static final String LOG_MAX_FILE_SIZE_SYS_PROPERTY = "applicationLogMaxFileSize";
  private static final String LOG_TOTAL_SIZE_CAP_SYS_PROPERTY = "applicationLogTotalSizeCap";
  // Floors so a tiny value can't cause a rollover storm or prune nearly all history.
  private static final String MIN_MAX_FILE_SIZE = "256MB";
  private static final String MIN_TOTAL_SIZE_CAP = "5GB";
  // An archive named before the rollover pattern gained %i; group 1 is the log type.
  private static final Pattern LEGACY_ARCHIVE_PATTERN =
      Pattern.compile("^.*(application|audit)-log-[^.]+\\.gz$");
  private static final int DEFAULT_MAX_HISTORY = 30; // logback.xml default
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

  /**
   * Applies the persisted application and audit logging configuration, reloading logback once for
   * both. Needed at startup because logback resolves these values from system properties when it
   * parses logback.xml, and Play's includeConfigProperties exports config keys under their own
   * names rather than as the properties logback.xml reads. Without this, anything set through the
   * logging_config APIs reverts to the logback.xml defaults on restart.
   */
  public static void applyPersistedLoggingConfig(
      SettableRuntimeConfigFactory sConfigFactory, Config config) {
    // need to configure this to simulate the effect of play.logger.includeConfigProperties=true
    configureFixedLogbackSystemProperties(config);
    String maxFileSize = getLoggingMaxFileSize(sConfigFactory);
    String totalSizeCap = getLoggingTotalSizeCap(sConfigFactory);
    try {
      checkLogSizeLimits(sConfigFactory, maxFileSize, totalSizeCap);
    } catch (IllegalArgumentException e) {
      LOG.warn(
          "Ignoring persisted log size limits, using logback.xml defaults: {}", e.getMessage());
      maxFileSize = null;
      totalSizeCap = null;
      System.clearProperty(LOG_MAX_FILE_SIZE_SYS_PROPERTY);
      System.clearProperty(LOG_TOTAL_SIZE_CAP_SYS_PROPERTY);
    }
    setApplicationLoggingSystemProperties(
        getLoggingLevel(sConfigFactory),
        getLoggingRolloverPattern(sConfigFactory),
        getLoggingMaxHistory(sConfigFactory),
        maxFileSize,
        totalSizeCap,
        getLoggingFileNamePrefix(sConfigFactory),
        false);
    setAuditLoggingSystemProperties(
        getAuditLoggingOutputToStdout(sConfigFactory),
        getAuditLoggingOutputToFile(sConfigFactory),
        getAuditLoggingRolloverPattern(sConfigFactory),
        getAuditLoggingMaxHistory(sConfigFactory),
        getAuditLoggingFileNamePrefix(sConfigFactory),
        false);
    try {
      restartLogback();
    } catch (JoranException ex) {
      // A bad persisted value must not keep YBA from starting.
      LOG.warn("Could not apply persisted logging config, using logback.xml defaults", ex);
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

  // Returns null for an empty value so that the system property is left unset and logback falls
  // back to the default in logback.xml. An empty property would reach FileSize.valueOf and fail.
  private static String getLoggingMaxFileSize(SettableRuntimeConfigFactory sConfigFactory) {
    String maxFileSize = sConfigFactory.globalRuntimeConf().getString(LOGGING_MAX_FILE_SIZE_KEY);
    return StringUtils.isEmpty(maxFileSize) ? null : maxFileSize;
  }

  private static String getLoggingTotalSizeCap(SettableRuntimeConfigFactory sConfigFactory) {
    String totalSizeCap = sConfigFactory.globalRuntimeConf().getString(LOGGING_TOTAL_SIZE_CAP_KEY);
    return StringUtils.isEmpty(totalSizeCap) ? null : totalSizeCap;
  }

  /**
   * Deletes archives without a rollover index once older than their log type's maxHistory days,
   * since logback only cleans up indexed names. Returns how many such archives remain.
   */
  public static int expireLegacyLogArchives(
      SettableRuntimeConfigFactory sConfigFactory, Config config) {
    if (!config.hasPath(LOG_OVERRIDE_PATH_SYS_PROPERTY)) {
      return 0;
    }
    File logDir = new File(config.getString(LOG_OVERRIDE_PATH_SYS_PROPERTY));
    File[] files = logDir.listFiles();
    if (files == null) {
      return 0;
    }
    Integer applicationMaxHistory = getLoggingMaxHistory(sConfigFactory);
    int auditMaxHistory = getAuditLoggingMaxHistory(sConfigFactory);
    long now = System.currentTimeMillis();
    int deleted = 0;
    int remaining = 0;
    for (File file : files) {
      Matcher matcher = LEGACY_ARCHIVE_PATTERN.matcher(file.getName());
      if (!file.isFile() || !matcher.matches()) {
        continue;
      }
      int maxHistory =
          "audit".equals(matcher.group(1))
              ? auditMaxHistory
              : (applicationMaxHistory == null ? DEFAULT_MAX_HISTORY : applicationMaxHistory);
      // As in logback, a maxHistory of 0 keeps everything.
      if (maxHistory <= 0 || now - file.lastModified() <= TimeUnit.DAYS.toMillis(maxHistory)) {
        remaining++;
        continue;
      }
      try {
        Files.deleteIfExists(file.toPath());
        deleted++;
      } catch (IOException e) {
        remaining++;
        LOG.warn("Could not delete legacy log archive {}", file, e);
      }
    }
    if (deleted > 0) {
      LOG.info("Deleted {} expired legacy log archives in {}", deleted, logDir);
    }
    return remaining;
  }

  /**
   * Throws IllegalArgumentException if a size is malformed or below its floor, or if totalSizeCap
   * is below maxFileSize. A lone totalSizeCap is compared with the saved maxFileSize.
   */
  public static void checkLogSizeLimits(
      SettableRuntimeConfigFactory sConfigFactory,
      @Nullable String maxFileSize,
      @Nullable String totalSizeCap) {
    if (maxFileSize == null && totalSizeCap != null) {
      maxFileSize = getLoggingMaxFileSize(sConfigFactory);
    }
    long maxFileSizeBytes = parseLogSize("maxFileSize", maxFileSize, MIN_MAX_FILE_SIZE);
    long totalSizeCapBytes = parseLogSize("totalSizeCap", totalSizeCap, MIN_TOTAL_SIZE_CAP);
    if (maxFileSize != null && totalSizeCap != null && totalSizeCapBytes < maxFileSizeBytes) {
      throw new IllegalArgumentException(
          String.format(
              "totalSizeCap %s must not be smaller than maxFileSize %s",
              totalSizeCap, maxFileSize));
    }
  }

  // Returns the size in bytes, or 0 when unset. Parses with logback's own parser.
  private static long parseLogSize(String fieldName, @Nullable String value, String minimum) {
    if (value == null) {
      return 0;
    }
    long bytes;
    try {
      bytes = FileSize.valueOf(value).getSize();
    } catch (IllegalArgumentException e) {
      throw new IllegalArgumentException(String.format("Incorrect %s %s", fieldName, value));
    }
    if (bytes < FileSize.valueOf(minimum).getSize()) {
      throw new IllegalArgumentException(
          String.format("%s %s must be at least %s", fieldName, value, minimum));
    }
    return bytes;
  }

  private static String getLoggingFileNamePrefix(SettableRuntimeConfigFactory sConfigFactory) {
    return sConfigFactory.globalRuntimeConf().getString(LOGGING_FILE_PREFIX_KEY);
  }

  private static String getAuditLoggingFileNamePrefix(SettableRuntimeConfigFactory sConfigFactory) {
    return sConfigFactory.globalRuntimeConf().getString(AUDIT_LOG_FILE_PREFIX_KEY);
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
      @Nullable String maxFileSize,
      @Nullable String totalSizeCap,
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
    if (maxFileSize != null) {
      sConfigFactory.globalRuntimeConf().setValue(LOGGING_MAX_FILE_SIZE_KEY, maxFileSize);
    }
    if (totalSizeCap != null) {
      sConfigFactory.globalRuntimeConf().setValue(LOGGING_TOTAL_SIZE_CAP_KEY, totalSizeCap);
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
      @Nullable String maxFileSize,
      @Nullable String totalSizeCap,
      @Nullable String applicationLogFileNamePrefix)
      throws JoranException {
    String curLevel = System.getProperty(APPLICATION_LOG_LEVEL_SYS_PROPERTY);
    String curRolloverPattern = System.getProperty(LOG_ROLLOVER_PATTERN_SYS_PROPERTY);
    String curMaxHistoryStr = System.getProperty(LOG_MAX_HISTORY_SYS_PROPERTY);
    String curMaxFileSize = System.getProperty(LOG_MAX_FILE_SIZE_SYS_PROPERTY);
    String curTotalSizeCap = System.getProperty(LOG_TOTAL_SIZE_CAP_SYS_PROPERTY);
    String curApplicationLogFileNamePrefix =
        System.getProperty(APPLICATION_LOG_FILE_PREFIX_SYS_PROPERTY);
    setApplicationLoggingSystemProperties(
        level,
        rolloverPattern,
        maxHistory,
        maxFileSize,
        totalSizeCap,
        applicationLogFileNamePrefix,
        false);
    LOG.debug(
        "Update logging context: '{}' '{}' '{}' '{}' '{}' '{}'"
            + " (current '{}' '{}' '{}' '{}' '{}' '{}')",
        level,
        rolloverPattern,
        maxHistory,
        maxFileSize,
        totalSizeCap,
        applicationLogFileNamePrefix,
        curLevel,
        curRolloverPattern,
        curMaxHistoryStr,
        curMaxFileSize,
        curTotalSizeCap,
        curApplicationLogFileNamePrefix);
    try {
      restartLogback();
    } catch (JoranException e) {
      // rollback to prev state
      setApplicationLoggingSystemProperties(
          curLevel,
          curRolloverPattern,
          curMaxHistoryStr == null ? null : Integer.valueOf(curMaxHistoryStr),
          curMaxFileSize,
          curTotalSizeCap,
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
          curAuditLogFileNamePrefix,
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
      @Nullable String maxFileSize,
      @Nullable String totalSizeCap,
      @Nullable String applicationLogFileNamePrefix,
      boolean eraseIfEmpty) {
    setSystemProperty(APPLICATION_LOG_LEVEL_SYS_PROPERTY, level, eraseIfEmpty);
    setSystemProperty(LOG_ROLLOVER_PATTERN_SYS_PROPERTY, rolloverPattern, eraseIfEmpty);
    setSystemProperty(
        LOG_MAX_HISTORY_SYS_PROPERTY,
        maxHistory == null ? null : String.valueOf(maxHistory),
        eraseIfEmpty);
    setSystemProperty(LOG_MAX_FILE_SIZE_SYS_PROPERTY, maxFileSize, eraseIfEmpty);
    setSystemProperty(LOG_TOTAL_SIZE_CAP_SYS_PROPERTY, totalSizeCap, eraseIfEmpty);
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
