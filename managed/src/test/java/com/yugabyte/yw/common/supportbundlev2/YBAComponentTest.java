// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import ch.qos.logback.classic.Level;
import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.BundleDetails.YbaComponentSpec;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveOutputStream;
import org.apache.commons.compress.compressors.gzip.GzipCompressorOutputStream;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import org.slf4j.LoggerFactory;

@RunWith(MockitoJUnitRunner.class)
public class YBAComponentTest extends FakeDBApplication {
  @Mock public ShellProcessHandler mockShellProcessHandler;
  @Mock public Config mockConfig;
  @Mock public RuntimeConfGetter mockConfGetter;
  @Mock public SupportBundleScriptEstimateSupport mockScriptEstimateSupport;

  private Universe universe;
  private Customer customer;
  private final String fakeBundleBasePath = "/tmp/yugaware_tests/support_bundle-yba_component/";
  private final String fakeBundlePath = fakeBundleBasePath + "bundle";
  private final String fakeTarPath = fakeBundleBasePath + "application-logs.tar.gz";
  private final String testRegexPattern = "application-log-\\d{4}-\\d{2}-\\d{2}(\\.gz)?";
  private final String testSdfPattern = "'application-log-'yyyy-MM-dd";
  private final Date testStartDate = new Date(1646438400000L); // 2022-03-05
  private final Date testEndDate = new Date(1646697600000L); // 2022-03-08
  private final List<UUID> bundleUuids = new ArrayList<>();

  @Before
  public void setUp() throws Exception {
    this.customer = ModelFactory.testCustomer();
    this.universe = ModelFactory.createUniverse(customer.getId());
    Path devopsHome = Paths.get(fakeBundleBasePath, "devops");
    Path scriptPath = devopsHome.resolve("bin/yba_utils.sh");
    Files.createDirectories(scriptPath.getParent());
    Files.createDirectories(Paths.get(fakeBundlePath));
    Files.writeString(scriptPath, "#!/bin/bash\necho stub\n");
    when(mockConfig.getString("yb.devops.home")).thenReturn(devopsHome.toString());
    when(mockConfig.getString("log.override.path")).thenReturn(fakeBundleBasePath + "logs");
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleApplicationLogsRegexPattern))
        .thenReturn(testRegexPattern);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleApplicationLogsSdfPattern))
        .thenReturn(testSdfPattern);
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(fakeBundleBasePath));
    // Only the task removes a bundle's staging root.
    for (UUID bundleUuid : bundleUuids) {
      FileUtils.deleteDirectory(SupportBundleStaging.rootFor(bundleUuid).toFile());
    }
    // Tests run with yb.storage.path=/tmp; don't leave the emptied staging root there.
    File stagingBase = SupportBundleStaging.rootFor(null).toFile();
    String[] left = stagingBase.list();
    if (left != null && left.length == 0) {
      stagingBase.delete();
    }
  }

  private YBAComponent newComponent() {
    return new YBAComponent(
        mockShellProcessHandler, mockScriptEstimateSupport, mockConfig, mockConfGetter);
  }

  private SupportBundleTaskParams paramsWithSpec(YbaComponentSpec spec) {
    return paramsWithSpec(spec, UUID.randomUUID());
  }

  private SupportBundleTaskParams paramsWithSpec(YbaComponentSpec spec, UUID bundleUuid) {
    bundleUuids.add(bundleUuid);
    SupportBundleFormData bundleData = new SupportBundleFormData();
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setYbaComponentSpecs(Collections.singletonList(spec));
    SupportBundle supportBundle =
        new SupportBundle(
            bundleUuid,
            universe.getUniverseUUID(),
            null,
            null,
            null,
            bundleDetails,
            SupportBundleStatusType.Running);
    return new SupportBundleTaskParams(supportBundle, bundleData, customer, universe);
  }

  private YbaComponentSpec baseSpec() {
    YbaComponentSpec spec = new YbaComponentSpec();
    spec.setComponentName("ApplicationLogs");
    spec.setScriptPath("bin/yba_utils.sh");
    spec.setParams(
        Arrays.asList(
            "create_application_logs_bundle", "--log_dir", "", "--remote_tar_path", fakeTarPath));
    spec.setRemoteTarPath(fakeTarPath);
    spec.setTimeoutSecs(60);
    return spec;
  }

  @Test
  public void testRemoteTarPathIsScopedToTheBundle() throws Exception {
    // ApplicationLogs runs on the YBA host, so its tar path is shared across universes as well as
    // across bundles -- two concurrent bundles would otherwise write the same file.
    UUID firstBundle = UUID.randomUUID();
    UUID secondBundle = UUID.randomUUID();

    String firstPath = tarPathPassedToScript(firstBundle);
    String secondPath = tarPathPassedToScript(secondBundle);

    assertTrue(firstPath, firstPath.contains(firstBundle.toString()));
    assertTrue(secondPath, secondPath.contains(secondBundle.toString()));
    assertNotEquals(firstPath, secondPath);
    // The script is exec'd as argv, so nothing would expand a token that survived this far.
    assertFalse(firstPath, firstPath.contains("${bundleUuid}"));
  }

  /** Runs the component for one bundle and returns the --remote_tar_path it handed the script. */
  private String tarPathPassedToScript(UUID bundleUuid) throws Exception {
    ShellProcessHandler handler = mock(ShellProcessHandler.class);
    when(handler.run(any(List.class), any(ShellProcessContext.class)))
        // EMPTY short-circuits before the untar, which is not what this test is about.
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=EMPTY"));

    YbaComponentSpec spec = baseSpec();
    String template = fakeBundleBasePath + "application-logs-${bundleUuid}.tar.gz";
    spec.setRemoteTarPath(template);
    spec.setParams(
        Arrays.asList(
            "create_application_logs_bundle", "--log_dir", "", "--remote_tar_path", template));

    new YBAComponent(handler, mockScriptEstimateSupport, mockConfig, mockConfGetter)
        .downloadComponentBetweenDates(
            paramsWithSpec(spec, bundleUuid),
            customer,
            universe,
            Paths.get(fakeBundlePath),
            testStartDate,
            testEndDate,
            null);

    ArgumentCaptor<List> argvCaptor = ArgumentCaptor.forClass(List.class);
    verify(handler).run(argvCaptor.capture(), any(ShellProcessContext.class));
    List<String> argv = argvCaptor.getValue();
    int flagIndex = argv.indexOf("--remote_tar_path");
    assertTrue("argv carried no --remote_tar_path: " + argv, flagIndex >= 0);
    return argv.get(flagIndex + 1);
  }

  private void mockTarAtPath() throws Exception {
    Path logFile =
        Paths.get(createTempFile(fakeBundleBasePath + "logs/", "application.log", "log-content"));
    try (OutputStream fOut = Files.newOutputStream(Paths.get(fakeTarPath));
        BufferedOutputStream buffOut = new BufferedOutputStream(fOut);
        GzipCompressorOutputStream gzOut = new GzipCompressorOutputStream(buffOut);
        TarArchiveOutputStream tOut = new TarArchiveOutputStream(gzOut)) {
      TarArchiveEntry tarEntry =
          new TarArchiveEntry(logFile.toFile(), "application_logs/application.log");
      tOut.putArchiveEntry(tarEntry);
      Files.copy(logFile, tOut);
      tOut.closeArchiveEntry();
      tOut.finish();
    }
  }

  @Test
  public void testDownloadComponentBetweenDatesSuccess() throws Exception {
    mockTarAtPath();
    when(mockShellProcessHandler.run(any(List.class), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=OK\nBUNDLE_PATH=" + fakeTarPath));

    YBAComponent component = newComponent();
    component.downloadComponentBetweenDates(
        paramsWithSpec(baseSpec()),
        customer,
        universe,
        Paths.get(fakeBundlePath),
        testStartDate,
        testEndDate,
        null);

    verify(mockShellProcessHandler, times(1)).run(any(List.class), any(ShellProcessContext.class));
    assertTrue(
        new File(Paths.get(fakeBundlePath, "application_logs", "application.log").toString())
            .exists());
    assertTrue(!new File(Paths.get(fakeBundlePath, "ApplicationLogs.tar.gz").toString()).exists());
  }

  /** A BUNDLE_PATH other than the requested path is neither copied into the bundle nor deleted. */
  @Test
  public void testDifferentReportedBundlePathIsNeitherCopiedNorDeleted() throws Exception {
    mockTarAtPath();
    Path victim = Paths.get(createTempFile(fakeBundleBasePath + "victim/", "keep.txt", "keep"));
    when(mockShellProcessHandler.run(any(List.class), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=OK\nBUNDLE_PATH=" + victim));

    newComponent()
        .downloadComponentBetweenDates(
            paramsWithSpec(baseSpec()),
            customer,
            universe,
            Paths.get(fakeBundlePath),
            testStartDate,
            testEndDate,
            null);

    assertEquals("keep", Files.readString(victim));
    assertTrue(
        new File(Paths.get(fakeBundlePath, "application_logs", "application.log").toString())
            .exists());
  }

  /** A gunzip failure leaves no archive in the bundle, no staging and no script tar. */
  @Test
  public void testCorruptTarLeavesNoArchiveBehind() throws Exception {
    Files.writeString(Paths.get(fakeTarPath), "not gzip");
    when(mockShellProcessHandler.run(any(List.class), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=OK"));
    UUID bundleUuid = UUID.randomUUID();

    collect(bundleUuid);

    assertEquals(List.of(), archivesUnder(Paths.get(fakeBundlePath)));
    assertEquals(0, stagedEntries(bundleUuid));
    assertFalse(Files.exists(Paths.get(fakeTarPath)));
  }

  /** The same when the untar fails part-way, after the gunzip has written a .tar. */
  @Test
  public void testFailedUntarLeavesNoArchiveBehind() throws Exception {
    mockTarAtPath();
    // A directory where the entry's file must go makes the untar throw.
    Files.createDirectories(Paths.get(fakeBundlePath, "application_logs", "application.log"));
    when(mockShellProcessHandler.run(any(List.class), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=OK"));
    UUID bundleUuid = UUID.randomUUID();

    collect(bundleUuid);

    assertEquals(List.of(), archivesUnder(Paths.get(fakeBundlePath)));
    assertEquals(0, stagedEntries(bundleUuid));
    assertFalse(Files.exists(Paths.get(fakeTarPath)));
  }

  @Test
  public void testNonZeroExitDeletesTheScriptTar() throws Exception {
    mockTarAtPath();
    when(mockShellProcessHandler.run(any(List.class), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(1, "tar: write error"));

    collect(UUID.randomUUID());

    assertFalse(Files.exists(Paths.get(fakeTarPath)));
  }

  /**
   * The script's TMPDIR is a directory YBA creates under the bundle's staging and deletes itself: a
   * timed-out script is killed with SIGKILL, so its own trap can't run.
   */
  @Test
  public void testScriptTmpDirIsYbasOwnAndRemoved() throws Exception {
    UUID bundleUuid = UUID.randomUUID();
    Path[] tmpDir = new Path[1];
    when(mockShellProcessHandler.run(any(List.class), any(ShellProcessContext.class)))
        .thenAnswer(
            invocation -> {
              ShellProcessContext context = invocation.getArgument(1);
              tmpDir[0] = Paths.get(context.getExtraEnvVars().get("TMPDIR"));
              Files.writeString(tmpDir[0].resolve("tmp.left"), "left by a killed script");
              return ShellResponse.create(137, "killed");
            });

    collect(bundleUuid);

    assertEquals(SupportBundleStaging.rootFor(bundleUuid), tmpDir[0].getParent());
    assertFalse(Files.exists(tmpDir[0]));
  }

  @Test
  public void testCancelledScriptIsAnAbortAndDeletesTheScriptTar() throws Exception {
    mockTarAtPath();
    when(mockShellProcessHandler.run(any(List.class), any(ShellProcessContext.class)))
        .thenReturn(
            ShellResponse.create(ShellResponse.ERROR_CODE_EXECUTION_CANCELLED, "cancelled"));

    assertThrows(CancellationException.class, () -> collect(UUID.randomUUID()));

    assertTrue(Thread.interrupted());
    assertFalse(Files.exists(Paths.get(fakeTarPath)));
  }

  @Test
  public void testInterruptedThreadRunsNoScript() throws Exception {
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.ybaComponentSpecs = List.of(baseSpec());

    Thread.currentThread().interrupt();
    assertThrows(CancellationException.class, () -> collect(UUID.randomUUID()));
    assertTrue(Thread.interrupted());
    Thread.currentThread().interrupt();
    assertThrows(
        CancellationException.class,
        () ->
            newComponent()
                .getFilesListWithSizes(
                    customer,
                    new SupportBundleFormData(),
                    bundleDetails,
                    universe,
                    testStartDate,
                    testEndDate,
                    null));
    assertTrue(Thread.interrupted());

    verify(mockShellProcessHandler, never()).run(any(List.class), any(ShellProcessContext.class));
    verify(mockScriptEstimateSupport, never())
        .runYbaScriptEstimate(any(), any(), any(), any(), any(), any());
  }

  @Test
  public void testFailedStatusDeletesTheScriptTar() throws Exception {
    mockTarAtPath();
    when(mockShellProcessHandler.run(any(List.class), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=FAILED"));

    collect(UUID.randomUUID());

    assertFalse(Files.exists(Paths.get(fakeTarPath)));
  }

  @Test
  public void testEntryOutsideTheBundleIsNotWritten() throws Exception {
    writeTarGz(
        Paths.get(fakeTarPath),
        Map.of("../escaped.txt", "x", "application_logs/application.log", "log-content"));
    when(mockShellProcessHandler.run(any(List.class), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=OK"));

    collect(UUID.randomUUID());

    assertFalse(Files.exists(Paths.get(fakeBundleBasePath, "escaped.txt")));
    assertTrue(Files.exists(Paths.get(fakeBundlePath, "application_logs", "application.log")));
  }

  private void collect(UUID bundleUuid) throws Exception {
    newComponent()
        .downloadComponentBetweenDates(
            paramsWithSpec(baseSpec(), bundleUuid),
            customer,
            universe,
            Paths.get(fakeBundlePath),
            testStartDate,
            testEndDate,
            null);
  }

  private static List<Path> archivesUnder(Path dir) throws IOException {
    try (Stream<Path> files = Files.walk(dir)) {
      return files
          .filter(p -> p.toString().endsWith(".tar") || p.toString().endsWith(".tar.gz"))
          .collect(Collectors.toList());
    }
  }

  private static long stagedEntries(UUID bundleUuid) throws IOException {
    Path root = SupportBundleStaging.rootFor(bundleUuid);
    if (!Files.exists(root)) {
      return 0;
    }
    try (Stream<Path> entries = Files.list(root)) {
      return entries.count();
    }
  }

  private static void writeTarGz(Path target, Map<String, String> entries) throws IOException {
    try (OutputStream fOut = Files.newOutputStream(target);
        GzipCompressorOutputStream gzOut =
            new GzipCompressorOutputStream(new BufferedOutputStream(fOut));
        TarArchiveOutputStream tOut = new TarArchiveOutputStream(gzOut)) {
      for (Map.Entry<String, String> e : entries.entrySet()) {
        byte[] content = e.getValue().getBytes(StandardCharsets.UTF_8);
        TarArchiveEntry entry = new TarArchiveEntry(e.getKey());
        entry.setSize(content.length);
        tOut.putArchiveEntry(entry);
        tOut.write(content);
        tOut.closeArchiveEntry();
      }
      tOut.finish();
    }
  }

  @Test
  public void testEmptyStatusSkipsUntar() throws Exception {
    when(mockShellProcessHandler.run(any(List.class), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=EMPTY"));

    YBAComponent component = newComponent();
    component.downloadComponentBetweenDates(
        paramsWithSpec(baseSpec()),
        customer,
        universe,
        Paths.get(fakeBundlePath),
        testStartDate,
        testEndDate,
        null);

    verify(mockShellProcessHandler, times(1)).run(any(List.class), any(ShellProcessContext.class));
    assertTrue(!new File(Paths.get(fakeBundlePath, "application_logs").toString()).exists());
  }

  @Test
  public void testUsesRemoteTarPathWhenNoBundlePath() throws Exception {
    mockTarAtPath();
    when(mockShellProcessHandler.run(any(List.class), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=OK"));

    YBAComponent component = newComponent();
    component.downloadComponentBetweenDates(
        paramsWithSpec(baseSpec()),
        customer,
        universe,
        Paths.get(fakeBundlePath),
        testStartDate,
        testEndDate,
        null);

    verify(mockShellProcessHandler, never())
        .run(eq(Collections.emptyList()), any(ShellProcessContext.class));
    assertTrue(
        new File(Paths.get(fakeBundlePath, "application_logs", "application.log").toString())
            .exists());
  }

  @Test
  public void testYbaComponentSpecSerializationRoundTrip() throws Exception {
    ObjectMapper mapper = new ObjectMapper();
    mapper.disable(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES);
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setYbaComponentSpecs(Collections.singletonList(baseSpec()));

    String json = mapper.writeValueAsString(bundleDetails);
    BundleDetails parsed = mapper.readValue(json, BundleDetails.class);

    assertNotNull(parsed.getYbaComponentSpecs());
    assertEquals(1, parsed.getYbaComponentSpecs().size());
    YbaComponentSpec parsedSpec = parsed.getYbaComponentSpecs().get(0);
    assertEquals("ApplicationLogs", parsedSpec.getComponentName());
    assertEquals("bin/yba_utils.sh", parsedSpec.getScriptPath());
    assertEquals(fakeTarPath, parsedSpec.getRemoteTarPath());
    assertEquals(60L, parsedSpec.getTimeoutSecs());
    List<String> params = parsedSpec.getParams();
    assertEquals("create_application_logs_bundle", params.get(0));
  }

  /** Runs {@code action} and returns the messages {@code cls} logged at {@code level}. */
  private static List<String> logged(Class<?> cls, Level level, ThrowingRunnable action)
      throws Exception {
    Logger logger = (Logger) LoggerFactory.getLogger(cls);
    ListAppender<ILoggingEvent> appender = new ListAppender<>();
    appender.start();
    logger.addAppender(appender);
    try {
      action.run();
    } finally {
      logger.detachAppender(appender);
    }
    return appender.list.stream()
        .filter(event -> event.getLevel() == level)
        .map(ILoggingEvent::getFormattedMessage)
        .collect(Collectors.toList());
  }

  private interface ThrowingRunnable {
    void run() throws Exception;
  }

  /** A YBA-host tar that can't be deleted is a WARN, not a DEBUG line. */
  @Test
  public void testFailedTarDeleteIsLoggedAtWarn() throws Exception {
    // A non-empty directory where the tar would be: deleteIfExists throws.
    Files.createDirectories(Paths.get(fakeTarPath, "blocker"));
    when(mockShellProcessHandler.run(any(List.class), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=EMPTY"));

    List<String> warnings =
        logged(AbstractScriptTarYbaComponent.class, Level.WARN, () -> collect(UUID.randomUUID()));

    assertTrue(
        warnings.toString(),
        warnings.stream()
            .anyMatch(w -> w.startsWith("Could not remove support bundle tar '" + fakeTarPath)));
  }
}
