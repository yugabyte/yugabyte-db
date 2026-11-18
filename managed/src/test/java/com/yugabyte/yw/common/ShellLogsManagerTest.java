// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import junit.framework.TestCase;
import org.apache.commons.lang3.tuple.Pair;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ShellLogsManagerTest extends TestCase {
  @Rule public TemporaryFolder tmpFolder = new TemporaryFolder();

  @Mock RuntimeConfigFactory mockRuntimeConfigFactory;

  @Mock Config mockConfig;

  @Mock RuntimeConfGetter runtimeConfGetter;

  @Mock PlatformScheduler mockPlatformScheduler;

  @Before
  public void beforeTest() {
    when(mockRuntimeConfigFactory.globalRuntimeConf()).thenReturn(mockConfig);
  }

  @Test
  public void testLogsRotationRemoveByTime() {
    Long baseTS = System.currentTimeMillis();
    testLogsRotation(
        2,
        null,
        baseTS,
        Arrays.asList(
            mockFile(baseTS, -1, 10),
            mockFile(baseTS, -2, 10),
            mockFile(baseTS, -3, 100),
            mockFile(baseTS, -4, 10),
            mockFile(baseTS, -1, 10)),
        1,
        2,
        3);
  }

  @Test
  public void testLogsRotationRemoveBySize() {
    Long baseTS = System.currentTimeMillis();
    testLogsRotation(
        null,
        120L,
        baseTS,
        Arrays.asList(
            mockFile(baseTS, -4, 10),
            mockFile(baseTS, -4, 20),
            mockFile(baseTS, -3, 100),
            mockFile(baseTS, -2, 10),
            mockFile(baseTS, -1, 1)),
        0,
        1);
  }

  @Test
  public void testLogsRotationRemoveBySizeAndTime() {
    Long baseTS = System.currentTimeMillis();
    testLogsRotation(
        4,
        20L,
        baseTS,
        Arrays.asList(
            mockFile(baseTS, -4, 10), // by time
            mockFile(baseTS, -4, 20), // by time
            mockFile(baseTS, -3, 100), // by size
            mockFile(baseTS, -3, 9),
            mockFile(baseTS, -2, 10),
            mockFile(baseTS, -1, 1)),
        0,
        1,
        2);
  }

  @Test
  public void testNotDeletingCurrentlyUsedLogs() throws IOException {
    Long baseTS = System.currentTimeMillis();
    List<File> files = new ArrayList<>();

    ShellLogsManager mockManager =
        new ShellLogsManager(mockPlatformScheduler, mockRuntimeConfigFactory, runtimeConfGetter) {
          @Override
          protected List<File> listLogFiles() {
            return files;
          }

          @Override
          protected File createLogsFile(boolean stderr) throws IOException {
            File file = mockFile(baseTS, -4, 10);
            files.add(file);
            when(file.delete())
                .then(
                    invocation -> {
                      files.remove(file);
                      return true;
                    });
            return file;
          }
        };
    UUID pid = UUID.randomUUID();
    mockManager.createFilesForProcess(pid);
    mockManager.rotateLogs(1L, 1);
    assertEquals(2, files.size()); // Files are not removed
    mockManager.onProcessStop(pid, false);
    mockManager.rotateLogs(1L, 1);
    assertTrue(files.isEmpty()); // Files are removed
  }

  @Test
  public void testForceDeleteRemovesOutputDespiteRetentionPolicy() throws IOException {
    when(mockConfig.getBoolean(ShellLogsManager.COMMAND_OUTPUT_LOGS_DELETE_KEY)).thenReturn(true);
    when(mockConfig.hasPath(ShellLogsManager.YB_LOGS_SHELL_OUTPUT_RETENTION_HOURS_KEY))
        .thenReturn(true);
    when(mockConfig.getInt(ShellLogsManager.YB_LOGS_SHELL_OUTPUT_RETENTION_HOURS_KEY))
        .thenReturn(1);
    ShellLogsManager manager = managerWritingTo(tmpFolder.getRoot());
    UUID retained = UUID.randomUUID();
    Pair<File, File> retainedFiles = manager.createFilesForProcess(retained);
    UUID forceDeleted = UUID.randomUUID();
    Pair<File, File> forceDeletedFiles = manager.createFilesForProcess(forceDeleted);

    manager.onProcessStop(retained, false);
    manager.onProcessStop(forceDeleted, true);

    assertTrue(retainedFiles.getLeft().exists());
    assertTrue(retainedFiles.getRight().exists());
    assertFalse(forceDeletedFiles.getLeft().exists());
    assertFalse(forceDeletedFiles.getRight().exists());
  }

  @Test
  public void testOutputFilesAreReadableOnlyByOwner() throws IOException {
    Pair<File, File> files =
        managerWritingTo(tmpFolder.getRoot()).createFilesForProcess(UUID.randomUUID());
    for (File file : List.of(files.getLeft(), files.getRight())) {
      assertEquals(
          PosixFilePermissions.fromString("rw-------"),
          Files.getPosixFilePermissions(file.toPath()));
    }
  }

  @Test
  public void testRotationFindsOutputFilesWhereTheyAreWritten() throws IOException {
    ShellLogsManager manager = managerWritingTo(tmpFolder.getRoot());
    UUID pid = UUID.randomUUID();
    Pair<File, File> files = manager.createFilesForProcess(pid);
    manager.onProcessStop(pid, false);
    assertTrue(files.getLeft().exists());

    manager.rotateLogs(null, 0);

    assertFalse(files.getLeft().exists());
    assertFalse(files.getRight().exists());
  }

  private ShellLogsManager managerWritingTo(File dir) {
    when(runtimeConfGetter.getGlobalConf(GlobalConfKeys.ybTmpDirectoryPath))
        .thenReturn(dir.getAbsolutePath());
    return new ShellLogsManager(mockPlatformScheduler, mockRuntimeConfigFactory, runtimeConfGetter);
  }

  private void testLogsRotation(
      @Nullable Integer retentionHours,
      @Nullable Long maxSize,
      Long baseTS,
      List<File> mockFiles,
      Integer... expectedRemove) {
    List<Integer> actuallyRemoved = new ArrayList<>();
    for (int i = 0; i < mockFiles.size(); i++) {
      File file = mockFiles.get(i);
      final int idx = i;
      when(file.delete())
          .then(
              invocation -> {
                actuallyRemoved.add(idx);
                return true;
              });
    }
    Collections.shuffle(mockFiles);
    ShellLogsManager mockManager =
        new ShellLogsManager(mockPlatformScheduler, mockRuntimeConfigFactory, runtimeConfGetter) {
          @Override
          protected Long getBaseTimestamp(long retentionHours) {
            return baseTS - TimeUnit.HOURS.toMillis(retentionHours);
          }

          @Override
          protected List<File> listLogFiles() {
            return mockFiles;
          }
        };
    mockManager.rotateLogs(maxSize, retentionHours);
    actuallyRemoved.sort(Comparator.naturalOrder());
    List<Integer> expected = Arrays.stream(expectedRemove).sorted().collect(Collectors.toList());
    assertEquals(expected, actuallyRemoved);
  }

  private File mockFile(long baseTs, int deltaTSHours, long size) {
    File file = mock(File.class);
    when(file.lastModified()).thenReturn(baseTs + TimeUnit.HOURS.toMillis(deltaTSHours));
    when(file.length()).thenReturn(size);
    when(file.getName()).thenReturn(UUID.randomUUID().toString());
    return file;
  }
}
