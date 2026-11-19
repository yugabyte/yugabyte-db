// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.logging;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import ch.qos.logback.classic.LoggerContext;
import ch.qos.logback.classic.util.ContextInitializer;
import ch.qos.logback.core.joran.spi.JoranException;
import com.google.common.collect.ImmutableSet;
import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.PlatformScheduler;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.FileTime;
import java.time.Duration;
import java.time.Instant;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import org.apache.commons.io.FileUtils;
import org.apache.pekko.actor.Cancellable;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.slf4j.LoggerFactory;

public class LogUtilTest extends FakeDBApplication {

  // Every system property applyPersistedLoggingConfig is expected to drive.
  private static final String[] LOGGING_SYS_PROPERTIES = {
    "applicationLogLevel",
    "applicationLogRolloverPattern",
    "applicationLogMaxHistory",
    "applicationLogMaxFileSize",
    "applicationLogTotalSizeCap",
    "applicationLogFileNamePrefix",
    "auditLogOutputToStdout",
    "auditLogOutputToFile",
    "auditLogRolloverPattern",
    "auditLogMaxHistory",
    "auditLogFileNamePrefix",
  };

  private final Map<String, String> savedProperties = new HashMap<>();

  @Before
  public void saveSystemProperties() {
    for (String key : LOGGING_SYS_PROPERTIES) {
      savedProperties.put(key, System.getProperty(key));
    }
  }

  /**
   * applyPersistedLoggingConfig reloads the logback context from the properties it sets, and
   * logback-test.xml reads applicationLogLevel for the console threshold. Restore the properties
   * and rebuild the context from them, or the rest of this JVM inherits the level set here.
   */
  @After
  public void restoreSystemProperties() throws JoranException {
    savedProperties.forEach(
        (key, value) -> {
          if (value == null) {
            System.clearProperty(key);
          } else {
            System.setProperty(key, value);
          }
        });
    LoggerContext loggerContext = (LoggerContext) LoggerFactory.getILoggerFactory();
    loggerContext.reset();
    new ContextInitializer(loggerContext).autoConfig();
  }

  @Test
  public void testApplyPersistedLoggingConfigSetsSizeLimits() {
    mutableConfigFactory.globalRuntimeConf().setValue("yb.logging.max_file_size", "512MB");
    mutableConfigFactory.globalRuntimeConf().setValue("yb.logging.total_size_cap", "6GB");
    mutableConfigFactory.globalRuntimeConf().setValue("yb.logging.max_history", "7");
    mutableConfigFactory.globalRuntimeConf().setValue("yb.logging.config", "INFO");

    LogUtil.applyPersistedLoggingConfig(
        mutableConfigFactory, mutableConfigFactory.staticApplicationConf());

    assertEquals("512MB", System.getProperty("applicationLogMaxFileSize"));
    assertEquals("6GB", System.getProperty("applicationLogTotalSizeCap"));
    assertEquals("7", System.getProperty("applicationLogMaxHistory"));
    assertEquals("INFO", System.getProperty("applicationLogLevel"));
  }

  @Test
  public void testApplyPersistedLoggingConfigKeepsFileNamePrefixes() {
    // Guards the reason the two former callerless methods were not simply wired up: they passed a
    // hardcoded "", which at startup would rename the active log files out from under an operator.
    mutableConfigFactory.globalRuntimeConf().setValue("yb.logging.fileNamePrefix", "yb-platform-");
    mutableConfigFactory.globalRuntimeConf().setValue("yb.audit.log.fileNamePrefix", "yb-audit-");

    LogUtil.applyPersistedLoggingConfig(
        mutableConfigFactory, mutableConfigFactory.staticApplicationConf());

    assertEquals("yb-platform-", System.getProperty("applicationLogFileNamePrefix"));
    assertEquals("yb-audit-", System.getProperty("auditLogFileNamePrefix"));
  }

  @Test
  public void testApplyPersistedLoggingConfigAppliesAuditSettings() {
    mutableConfigFactory.globalRuntimeConf().setValue("yb.audit.log.outputToStdout", "true");
    mutableConfigFactory.globalRuntimeConf().setValue("yb.audit.log.outputToFile", "false");
    mutableConfigFactory.globalRuntimeConf().setValue("yb.audit.log.maxHistory", "14");

    LogUtil.applyPersistedLoggingConfig(
        mutableConfigFactory, mutableConfigFactory.staticApplicationConf());

    assertEquals("true", System.getProperty("auditLogOutputToStdout"));
    assertEquals("false", System.getProperty("auditLogOutputToFile"));
    assertEquals("14", System.getProperty("auditLogMaxHistory"));
  }

  @Test
  public void testApplyPersistedLoggingConfigLeavesSizeLimitsUnsetWhenEmpty() {
    // An empty value must leave the property unset so logback falls back to its own default. An
    // empty property would reach FileSize.valueOf and fail the reload.
    System.clearProperty("applicationLogMaxFileSize");
    System.clearProperty("applicationLogTotalSizeCap");
    mutableConfigFactory.globalRuntimeConf().setValue("yb.logging.max_file_size", "");
    mutableConfigFactory.globalRuntimeConf().setValue("yb.logging.total_size_cap", "");

    LogUtil.applyPersistedLoggingConfig(
        mutableConfigFactory, mutableConfigFactory.staticApplicationConf());

    assertNull(System.getProperty("applicationLogMaxFileSize"));
    assertNull(System.getProperty("applicationLogTotalSizeCap"));
  }

  @Test
  public void testApplyPersistedLoggingConfigIgnoresSizeLimitsBelowMinimum() {
    // Both limits fall back to the logback.xml defaults, even the valid totalSizeCap.
    System.setProperty("applicationLogMaxFileSize", "2GB");
    System.setProperty("applicationLogTotalSizeCap", "20GB");
    mutableConfigFactory.globalRuntimeConf().setValue("yb.logging.max_file_size", "100MB");
    mutableConfigFactory.globalRuntimeConf().setValue("yb.logging.total_size_cap", "6GB");

    LogUtil.applyPersistedLoggingConfig(
        mutableConfigFactory, mutableConfigFactory.staticApplicationConf());

    assertNull(System.getProperty("applicationLogMaxFileSize"));
    assertNull(System.getProperty("applicationLogTotalSizeCap"));
  }

  @Test
  public void testExpireLegacyLogArchives() throws IOException {
    mutableConfigFactory.globalRuntimeConf().setValue("yb.logging.max_history", "30");
    mutableConfigFactory.globalRuntimeConf().setValue("yb.audit.log.maxHistory", "14");
    Path logDir = Files.createTempDirectory("legacy-log-archives");
    try {
      createAgedFile(logDir, "application-log-2026-08-01.gz", 40);
      createAgedFile(logDir, "yb-platform-application-log-2026-08-02.gz", 40);
      createAgedFile(logDir, "application-log-2026-09-25.gz", 20);
      createAgedFile(logDir, "audit-log-2026-09-25.gz", 20);
      createAgedFile(logDir, "audit-log-2026-10-15.gz", 5);
      // None of these is a legacy archive, so all must stay.
      createAgedFile(logDir, "application-log-2026-08-01.0.gz", 40);
      createAgedFile(logDir, "application-log-2026-08-01.01234567.tmp", 40);
      createAgedFile(logDir, "prometheus.log-20260801.gz", 40);
      createAgedFile(logDir, "other-log-2026-08-01.gz", 40);
      createAgedFile(logDir, "application.log", 40);

      int remaining = LogUtil.expireLegacyLogArchives(mutableConfigFactory, logDirConfig(logDir));

      assertEquals(2, remaining);
      assertEquals(
          ImmutableSet.of(
              "application-log-2026-09-25.gz",
              "audit-log-2026-10-15.gz",
              "application-log-2026-08-01.0.gz",
              "application-log-2026-08-01.01234567.tmp",
              "prometheus.log-20260801.gz",
              "other-log-2026-08-01.gz",
              "application.log"),
          listFileNames(logDir));
    } finally {
      FileUtils.deleteDirectory(logDir.toFile());
    }
  }

  @Test
  public void testExpireLegacyLogArchivesKeepsAllWhenMaxHistoryIsZero() throws IOException {
    mutableConfigFactory.globalRuntimeConf().setValue("yb.logging.max_history", "0");
    Path logDir = Files.createTempDirectory("legacy-log-archives");
    try {
      createAgedFile(logDir, "application-log-2026-08-01.gz", 400);

      int remaining = LogUtil.expireLegacyLogArchives(mutableConfigFactory, logDirConfig(logDir));

      assertEquals(1, remaining);
      assertEquals(ImmutableSet.of("application-log-2026-08-01.gz"), listFileNames(logDir));
    } finally {
      FileUtils.deleteDirectory(logDir.toFile());
    }
  }

  @Test
  public void testLegacyLogArchiveCleanupRunsDailyUntilNoneLeft() throws IOException {
    mutableConfigFactory.globalRuntimeConf().setValue("yb.logging.max_history", "30");
    Path logDir = Files.createTempDirectory("legacy-log-archives");
    try {
      PlatformScheduler scheduler = mock(PlatformScheduler.class);
      Cancellable cancellable = mock(Cancellable.class);
      when(scheduler.scheduleAlwaysOn(anyString(), any(), any(), any())).thenReturn(cancellable);
      Path archive = createAgedFile(logDir, "application-log-2026-10-10.gz", 10);
      LegacyLogArchiveCleanup cleanup =
          new LegacyLogArchiveCleanup(scheduler, mutableConfigFactory, logDirConfig(logDir));

      cleanup.start();
      verify(scheduler)
          .scheduleAlwaysOn(
              eq("LegacyLogArchiveCleanup"), eq(Duration.ofDays(1)), eq(Duration.ofDays(1)), any());
      cleanup.scheduleRunner();
      verify(cancellable, never()).cancel();

      setAge(archive, 40);
      cleanup.scheduleRunner();
      verify(cancellable).cancel();
      assertEquals(ImmutableSet.of(), listFileNames(logDir));

      // A startup with nothing to expire never schedules the job.
      PlatformScheduler idleScheduler = mock(PlatformScheduler.class);
      new LegacyLogArchiveCleanup(idleScheduler, mutableConfigFactory, logDirConfig(logDir))
          .start();
      verify(idleScheduler, never()).scheduleAlwaysOn(anyString(), any(), any(), any());
    } finally {
      FileUtils.deleteDirectory(logDir.toFile());
    }
  }

  private static Config logDirConfig(Path logDir) {
    return ConfigFactory.parseMap(Collections.singletonMap("log.override.path", logDir.toString()));
  }

  private static Path createAgedFile(Path dir, String name, int ageDays) throws IOException {
    Path file = Files.createFile(dir.resolve(name));
    setAge(file, ageDays);
    return file;
  }

  private static void setAge(Path file, int ageDays) throws IOException {
    Files.setLastModifiedTime(file, FileTime.from(Instant.now().minus(Duration.ofDays(ageDays))));
  }

  private static Set<String> listFileNames(Path dir) throws IOException {
    try (Stream<Path> paths = Files.list(dir)) {
      return paths.map(p -> p.getFileName().toString()).collect(Collectors.toSet());
    }
  }
}
