/*
 * Copyright 2020 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.ha;

import static junit.framework.TestCase.assertTrue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyBoolean;
import static org.mockito.Mockito.anyInt;
import static org.mockito.Mockito.anyList;
import static org.mockito.Mockito.anyMap;
import static org.mockito.Mockito.anyString;
import static org.mockito.Mockito.doCallRealMethod;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PrometheusConfigHelper;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.services.FileDataService;
import java.io.File;
import java.net.URL;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;

@RunWith(JUnitParamsRunner.class)
public class PlatformReplicationManagerTest extends FakeDBApplication {
  @Mock Config mockConfig;

  @Mock PlatformScheduler mockPlatformScheduler;

  @Mock ShellProcessHandler shellProcessHandler;

  @Mock RuntimeConfigFactory mockRuntimeConfigFactory;

  @Mock RuntimeConfGetter runtimeConfGetter;

  @Mock PlatformReplicationHelper mockReplicationUtil;

  @Mock ConfigHelper mockConfigHelper;

  @Mock FileDataService mockFileDataService;

  @Mock PrometheusConfigHelper mockPrometheusConfigHelper;

  private static final String STORAGE_PATH = "yb.storage.path";
  private static final String PG_DUMP_PATH = "/tmp/pg_dump";
  private static final String PG_RESTORE_PATH = "/tmp/pg_restore";
  private static final String BASE_INSTALL = "/tmp/yugabyte";

  @Before
  public void setUp() {
    MockitoAnnotations.initMocks(this);
    when(mockConfig.getString(STORAGE_PATH)).thenReturn("/tmp");
  }

  private void setupConfig(
      String prometheusHost,
      String dbUsername,
      String dbPassword,
      String dbHost,
      int dbPort,
      boolean isYbaInstaller) {
    when(mockReplicationUtil.getBackupDir()).thenReturn(new File("/tmp/foo.bar").toPath());
    when(mockPrometheusConfigHelper.getPrometheusHost()).thenReturn(prometheusHost);
    when(mockPrometheusConfigHelper.getPrometheusPort()).thenReturn(9090);
    when(mockReplicationUtil.getDBHost()).thenReturn(dbHost);
    when(mockReplicationUtil.getDBPort()).thenReturn(dbPort);
    when(mockReplicationUtil.getDBUser()).thenReturn(dbUsername);
    when(mockReplicationUtil.getDBPassword()).thenReturn(dbPassword);
    when(mockReplicationUtil.isBackupScriptOutputEnabled()).thenReturn(false);
    if (isYbaInstaller) {
      when(mockReplicationUtil.getInstallationType()).thenReturn("yba-installer");
    } else {
      when(mockReplicationUtil.getInstallationType()).thenReturn("");
    }
    when(mockReplicationUtil.getPGDumpPath()).thenReturn(PG_DUMP_PATH);
    when(mockReplicationUtil.getPGRestorePath()).thenReturn(PG_RESTORE_PATH);
    when(mockReplicationUtil.getBaseInstall()).thenReturn(BASE_INSTALL);
  }

  private List<String> getExpectedPlatformBackupCommandArgs(
      String prometheusHost,
      String dbUsername,
      String dbHost,
      int dbPort,
      String inputPath,
      boolean isCreate,
      String backupDir,
      boolean isYbaInstaller) {
    List<String> expectedCommandArgs = new ArrayList<>();
    expectedCommandArgs.add("bin/yb_platform_backup.sh");
    if (isCreate) {
      expectedCommandArgs.add("create");
      expectedCommandArgs.add("--exclude_prometheus");
      expectedCommandArgs.add("--exclude_releases");
      expectedCommandArgs.add("--disable_version_check");
      if (isYbaInstaller) {
        expectedCommandArgs.add("--pg_dump_path");
        expectedCommandArgs.add(PG_DUMP_PATH);
        expectedCommandArgs.add("--yba_installer");
        expectedCommandArgs.add("--data_dir");
        expectedCommandArgs.add(BASE_INSTALL);
      }
      expectedCommandArgs.add("--output");
      expectedCommandArgs.add(backupDir);
    } else {
      expectedCommandArgs.add("restore");
      expectedCommandArgs.add("--input");
      expectedCommandArgs.add(inputPath);
      expectedCommandArgs.add("--disable_version_check");
      if (isYbaInstaller) {
        expectedCommandArgs.add("--pg_restore_path");
        expectedCommandArgs.add(PG_RESTORE_PATH);
        expectedCommandArgs.add("--yba_installer");
        expectedCommandArgs.add("--data_dir");
        expectedCommandArgs.add(BASE_INSTALL);
        expectedCommandArgs.add("--destination");
        expectedCommandArgs.add(BASE_INSTALL);
      }
    }

    expectedCommandArgs.add("--db_username");
    expectedCommandArgs.add(dbUsername);
    expectedCommandArgs.add("--db_host");
    expectedCommandArgs.add(dbHost);
    expectedCommandArgs.add("--db_port");
    expectedCommandArgs.add(Integer.toString(dbPort));
    expectedCommandArgs.add("--prometheus_host");
    expectedCommandArgs.add(prometheusHost);
    expectedCommandArgs.add("--prometheus_port");
    expectedCommandArgs.add("9090");
    expectedCommandArgs.add("--verbose");
    expectedCommandArgs.add("--skip_restart");

    return expectedCommandArgs;
  }

  @SuppressWarnings("unused")
  private Object[] parametersToTestCreatePlatformBackupParams() {
    return new Object[][] {
      {"1.2.3.4", "postgres", "password", "localhost", 5432, new File("/tmp/foo.bar"), true, false},
      {"1.2.3.4", "yugabyte", "", "5.6.7.8", 5433, new File("/tmp/foo.bar"), true, false},
      {
        "1.2.3.4", "postgres", "password", "localhost", 5432, new File("/tmp/foo.bar"), false, false
      },
      {"1.2.3.4", "yugabyte", "", "5.6.7.8", 5433, new File("/tmp/foo.bar"), false, false},
      {"1.2.3.4", "yugabyte", "", "5.6.7.8", 5433, new File("/tmp/foo.bar"), true, true},
      {"1.2.3.4", "yugabyte", "", "5.6.7.8", 5433, new File("/tmp/foo.bar"), false, true}
    };
  }

  @Parameters(method = "parametersToTestCreatePlatformBackupParams")
  @Test
  public void testCreatePlatformBackupParams(
      String prometheusHost,
      String dbUsername,
      String dbPassword,
      String dbHost,
      int dbPort,
      File inputPath,
      boolean isCreate,
      boolean isYbaInstaller) {
    Map<String, String> expectedEnvVars = new HashMap<>();
    if (!dbPassword.isEmpty()) {
      expectedEnvVars.put(PlatformReplicationManager.DB_PASSWORD_ENV_VAR_KEY, dbPassword);
    }

    when(shellProcessHandler.run(anyList(), anyMap(), anyBoolean()))
        .thenReturn(new ShellResponse());
    when(mockRuntimeConfigFactory.globalRuntimeConf()).thenReturn(mockConfig);
    when(runtimeConfGetter.getStaticConf()).thenReturn(mockConfig);
    mockReplicationUtil.shellProcessHandler = shellProcessHandler;
    doCallRealMethod()
        .when(mockReplicationUtil)
        .runCommand(any(PlatformReplicationManager.PlatformBackupParams.class));
    setupConfig(prometheusHost, dbUsername, dbPassword, dbHost, dbPort, isYbaInstaller);
    PlatformReplicationManager backupManager =
        new PlatformReplicationManager(
            mockPlatformScheduler,
            mockReplicationUtil,
            mockFileDataService,
            mockPrometheusConfigHelper);

    List<String> expectedCommandArgs =
        getExpectedPlatformBackupCommandArgs(
            prometheusHost,
            dbUsername,
            dbHost,
            dbPort,
            inputPath.getAbsolutePath(),
            isCreate,
            "/tmp/foo.bar",
            isYbaInstaller);

    if (isCreate) {
      backupManager.createBackup();
    } else {
      backupManager.restoreBackup(inputPath);
    }

    verify(shellProcessHandler, times(1)).run(expectedCommandArgs, expectedEnvVars, false);
  }

  @SuppressWarnings("unused")
  private Object[] parametersToTestGCBackups() {
    return new Object[][] {
      {-1}, {0}, {1}, {2}, {3}, {4},
    };
  }

  @Parameters(method = "parametersToTestGCBackups")
  @Test
  public void testGCBackups(int numToRetain) throws Exception {
    File testFile1 = File.createTempFile("backup_1", ".tgz");
    File testFile2 = File.createTempFile("backup_2", ".tgz");
    File testFile3 = File.createTempFile("backup_3", ".tgz");
    try {
      String testAddr = "http://test.com";
      URL testUrl = new URL(testAddr);
      Path tmpDir = testFile1.toPath().getParent();
      when(mockRuntimeConfigFactory.globalRuntimeConf()).thenReturn(mockConfig);
      when(runtimeConfGetter.getStaticConf()).thenReturn(mockConfig);
      when(mockReplicationUtil.getNumBackupsRetention()).thenReturn(Math.max(0, numToRetain));
      when(mockReplicationUtil.getReplicationDirFor(anyString())).thenReturn(tmpDir);
      doCallRealMethod().when(mockReplicationUtil).cleanupBackups(anyList(), anyInt());
      doCallRealMethod().when(mockReplicationUtil).cleanupReceivedBackups(any(URL.class), anyInt());
      doCallRealMethod().when(mockReplicationUtil).listBackups(any(URL.class));
      PlatformReplicationManager backupManager =
          spy(
              new PlatformReplicationManager(
                  mockPlatformScheduler,
                  mockReplicationUtil,
                  mockFileDataService,
                  mockPrometheusConfigHelper));

      List<File> backups = backupManager.listBackups(testUrl);
      assertEquals(3, backups.size());

      backupManager.cleanupReceivedBackups(testUrl);

      backups = backupManager.listBackups(testUrl);
      assertEquals(Math.max(0, Math.min(numToRetain, 3)), backups.size());
      if (numToRetain == 1) {
        assertFalse(testFile1.exists());
        assertFalse(testFile2.exists());
        assertTrue(testFile3.exists());
      } else if (numToRetain == 2) {
        assertFalse(testFile1.exists());
        assertTrue(testFile2.exists());
        assertTrue(testFile3.exists());
      } else if (numToRetain >= 3) {
        assertTrue(testFile1.exists());
        assertTrue(testFile2.exists());
        assertTrue(testFile3.exists());
      } else {
        assertFalse(testFile1.exists());
        assertFalse(testFile2.exists());
        assertFalse(testFile3.exists());
      }
    } finally {
      testFile1.delete();
      testFile2.delete();
      testFile3.delete();
    }
  }
}
