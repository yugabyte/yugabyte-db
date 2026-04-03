// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.gflags;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.concurrent.KeyLock;
import com.yugabyte.yw.models.Universe;
import java.io.File;
import java.io.IOException;
import java.lang.reflect.Field;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import junitparams.JUnitParamsRunner;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import org.junit.runner.RunWith;
import org.mockito.Spy;
import play.Environment;
import play.Mode;

@Slf4j
@RunWith(JUnitParamsRunner.class)
public class GFlagsValidationTest extends FakeDBApplication {
  private GFlagsValidation gFlagsValidation;
  private Universe universe;
  @Spy Environment environment;

  @Rule public TemporaryFolder tempFolder = new TemporaryFolder();

  @Before
  public void setUp() {
    ClassLoader classLoader = Thread.currentThread().getContextClassLoader();
    this.environment = new Environment(new File("."), classLoader, Mode.TEST);
    this.gFlagsValidation = new GFlagsValidation(environment, null, null);
    this.universe = ModelFactory.createUniverse(ModelFactory.testCustomer().getId());
    TestHelper.updateUniverseVersion(universe, "2.23.0.0-b1");
  }

  @Test
  public void testValidateConnectionPoolingGflags() {
    // Should not throw error for valid allowed gflags case.
    Map<String, String> connectionPoolingGflagsMap = new HashMap<String, String>();
    connectionPoolingGflagsMap.put("ysql_conn_mgr_max_client_connections", "10001");
    connectionPoolingGflagsMap.put("ysql_conn_mgr_idle_time", "30");
    connectionPoolingGflagsMap.put("ysql_conn_mgr_stats_interval", "20");
    SpecificGFlags connectionPoolingGflagsSpecificGflags =
        SpecificGFlags.construct(connectionPoolingGflagsMap, connectionPoolingGflagsMap);
    Map<UUID, SpecificGFlags> connectionPoolingGflags = new HashMap<UUID, SpecificGFlags>();
    connectionPoolingGflags.put(
        universe.getUniverseDetails().getPrimaryCluster().uuid,
        connectionPoolingGflagsSpecificGflags);
    gFlagsValidation.validateConnectionPoolingGflags(universe, connectionPoolingGflags);

    // Should not throw error for correct new version.
    TestHelper.updateUniverseVersion(universe, "2.25.0.0-b1");
    gFlagsValidation.validateConnectionPoolingGflags(universe, connectionPoolingGflags);

    // Throw error for invalid gflag.
    connectionPoolingGflags
        .get(universe.getUniverseDetails().getPrimaryCluster().uuid)
        .getPerProcessFlags()
        .value
        .get(ServerType.TSERVER)
        .put("random_key", "random_value");
    assertThrows(
        PlatformServiceException.class,
        () -> gFlagsValidation.validateConnectionPoolingGflags(universe, connectionPoolingGflags));

    // Should not throw error for some gflag that starts with prefix "ysql_conn_mgr".
    // Mostly for future cases where DB might add new CP gflags without updating YBA metadata.
    connectionPoolingGflags
        .get(universe.getUniverseDetails().getPrimaryCluster().uuid)
        .getPerProcessFlags()
        .value
        .get(ServerType.TSERVER)
        .remove("random_key");
    connectionPoolingGflags
        .get(universe.getUniverseDetails().getPrimaryCluster().uuid)
        .getPerProcessFlags()
        .value
        .get(ServerType.TSERVER)
        .put("ysql_conn_mgr_future_key", "100");
    gFlagsValidation.validateConnectionPoolingGflags(universe, connectionPoolingGflags);

    connectionPoolingGflags.put(
        UUID.fromString("00000000-0000-0000-0000-000000000000"),
        connectionPoolingGflagsSpecificGflags);
    assertThrows(
        PlatformServiceException.class,
        () -> gFlagsValidation.validateConnectionPoolingGflags(universe, connectionPoolingGflags));
  }

  private void createGFlagFiles(String releasesPath, String version, List<String> files)
      throws IOException {
    Path versionDir = Files.createDirectories(Paths.get(releasesPath, version));
    for (String file : files) {
      Files.createFile(versionDir.resolve(file));
    }
  }

  @Test
  public void testFetchGFlagFiles_differentVersionsConcurrently() throws Exception {
    String releasesPath = tempFolder.getRoot().getAbsolutePath();
    List<String> requiredFiles =
        Arrays.asList(
            GFlagsValidation.MASTER_GFLAG_FILE_NAME, GFlagsValidation.TSERVER_GFLAG_FILE_NAME);
    createGFlagFiles(releasesPath, "2.20.0.0", requiredFiles);
    createGFlagFiles(releasesPath, "2.22.0.0", requiredFiles);

    CountDownLatch startLatch = new CountDownLatch(1);
    ExecutorService executor = Executors.newFixedThreadPool(2);
    try {
      Future<?> f1 =
          executor.submit(
              () -> {
                try {
                  startLatch.await();
                  gFlagsValidation.fetchGFlagFilesFromTarGZipInputStream(
                      null, "2.20.0.0", requiredFiles, releasesPath);
                } catch (Exception e) {
                  throw new RuntimeException(e);
                }
              });
      Future<?> f2 =
          executor.submit(
              () -> {
                try {
                  startLatch.await();
                  gFlagsValidation.fetchGFlagFilesFromTarGZipInputStream(
                      null, "2.22.0.0", requiredFiles, releasesPath);
                } catch (Exception e) {
                  throw new RuntimeException(e);
                }
              });

      startLatch.countDown();
      f1.get(5, TimeUnit.SECONDS);
      f2.get(5, TimeUnit.SECONDS);
    } finally {
      executor.shutdownNow();
    }
  }

  @SuppressWarnings("unchecked")
  @Test
  public void testFetchGFlagFiles_sameVersionWaitsOnLock() throws Exception {
    String releasesPath = tempFolder.getRoot().getAbsolutePath();
    String version = "2.20.0.0";
    List<String> requiredFiles =
        Arrays.asList(
            GFlagsValidation.MASTER_GFLAG_FILE_NAME, GFlagsValidation.TSERVER_GFLAG_FILE_NAME);
    createGFlagFiles(releasesPath, version, requiredFiles);

    Field versionKeyLockField = GFlagsValidation.class.getDeclaredField("versionKeyLock");
    versionKeyLockField.setAccessible(true);
    KeyLock<String> versionKeyLock = (KeyLock<String>) versionKeyLockField.get(gFlagsValidation);
    versionKeyLock.acquireLock(version);
    CountDownLatch threadStarted = new CountDownLatch(1);
    CountDownLatch threadCompleted = new CountDownLatch(1);

    Thread t =
        new Thread(
            () -> {
              try {
                threadStarted.countDown();
                gFlagsValidation.fetchGFlagFilesFromTarGZipInputStream(
                    null, version, requiredFiles, releasesPath);
                threadCompleted.countDown();
              } catch (Exception e) {
                throw new RuntimeException(e);
              }
            });
    t.start();
    threadStarted.await();

    assertFalse(
        "Thread should be blocked on the version lock",
        threadCompleted.await(500, TimeUnit.MILLISECONDS));
    assertTrue(versionKeyLock.hasQueuedThreads());

    versionKeyLock.releaseLock(version);

    assertTrue(
        "Thread should complete after lock is released",
        threadCompleted.await(5, TimeUnit.SECONDS));
  }
}
