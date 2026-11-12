// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import static com.yugabyte.yw.common.TestHelper.createTarGzipFiles;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.notNull;
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
import com.typesafe.config.ConfigFactory;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ImageBundleUtil;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.supportbundlev2.cgroup.SupportBundleCgroupScriptExecutor;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestLoader;
import com.yugabyte.yw.common.supportbundlev2.nodebin.SupportBundleCdcStateDumpSupport;
import com.yugabyte.yw.common.supportbundlev2.nodebin.SupportBundleLogParseSupport;
import com.yugabyte.yw.common.supportbundlev2.nodebin.SupportBundleServerConfRedactionSupport;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.SupportBundle.SupportBundleStatusType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.BundleDetails.FilesComponentSpec;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.modules.CustomObjectMapperModule;
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
import java.util.HashSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;
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
public class FilesComponentTest extends FakeDBApplication {
  @Mock public NodeUniverseManager mockNodeUniverseManager;
  @Mock public ImageBundleUtil mockImageBundleUtil;
  @Mock public SupportBundleCgroupScriptExecutor mockCgroupScriptExecutor;
  @Mock public SupportBundleLogParseSupport mockLogParseSupport;
  @Mock public SupportBundleCdcStateDumpSupport mockCdcStateDumpSupport;
  @Mock public SupportBundleServerConfRedactionSupport mockServerConfRedactionSupport;
  @Mock public SupportBundleScriptEstimateSupport mockScriptEstimateSupport;
  @Mock public Config mockConfig;
  @Mock public RuntimeConfGetter mockConfGetter;

  // The components the shipped manifest marks redactServerConf.
  private static final Set<String> SERVER_CONF_REDACT_COMPONENTS = Set.of("GFlags", "YbcLogs");
  private static final String DROP_SERVER_CONF_PARAM = "--drop_server_conf";

  private Universe universe;
  private Customer customer;
  private final String fakeBundleBasePath = "/tmp/yugaware_tests/support_bundle-files_component/";
  private final String fakeSourcePath = fakeBundleBasePath + "source/";
  private final String fakeBundlePath = fakeBundleBasePath + "bundle";
  private final NodeDetails node = new NodeDetails();
  private final List<UUID> bundleUuids = new ArrayList<>();

  @Before
  public void setUp() throws Exception {
    this.customer = ModelFactory.testCustomer();
    this.universe = ModelFactory.createUniverse(customer.getId());

    node.nodeName = "u-n1";
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "fake_ip";
    this.universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            (universe) -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.nodeDetailsSet = new HashSet<>(Arrays.asList(node));
              universe.setUniverseDetails(universeDetails);
            });

    when(mockConfig.getString("yb.devops.home")).thenReturn(fakeBundleBasePath + "devops");
    when(mockConfig.getString("yb.support_bundle.default_mount_point_prefix")).thenReturn("/mnt/d");

    org.mockito.Mockito.when(
            mockCgroupScriptExecutor.executeScript(
                any(),
                any(),
                any(),
                any(),
                any(),
                any(),
                any(),
                any(),
                any(),
                org.mockito.ArgumentMatchers.anyBoolean()))
        .thenAnswer(
            invocation -> {
              NodeDetails n = invocation.getArgument(2);
              Universe u = invocation.getArgument(1);
              String scriptPathRelative = invocation.getArgument(3);
              List<String> params = invocation.getArgument(5);
              com.yugabyte.yw.common.ShellProcessContext ctx = invocation.getArgument(6);
              return mockNodeUniverseManager.runScript(n, u, scriptPathRelative, params, ctx);
            });
    // "/tmp" keeps a yugabyte spec's tar where the spec put it; tests that check the move pass
    // their own directory.
    org.mockito.Mockito.lenient()
        .when(mockCgroupScriptExecutor.stagingDir(any(), any(), any(), any()))
        .thenReturn("/tmp");
    org.mockito.Mockito.when(mockServerConfRedactionSupport.isServerConfRedactComponent(any()))
        .thenAnswer(
            invocation ->
                SERVER_CONF_REDACT_COMPONENTS.contains((String) invocation.getArgument(0)));
    org.mockito.Mockito.when(
            mockServerConfRedactionSupport.appendRedactionScriptParams(any(), any(), any(), any()))
        .thenAnswer(invocation -> invocation.getArgument(1));
    org.mockito.Mockito.when(mockServerConfRedactionSupport.appendDropServerConfScriptParam(any()))
        .thenAnswer(
            invocation -> {
              List<String> appended = new ArrayList<>(invocation.getArgument(0));
              appended.add(DROP_SERVER_CONF_PARAM);
              return appended;
            });
    org.mockito.Mockito.when(
            mockServerConfRedactionSupport.ensureRedactionBinaryOnNode(any(), any(), any(), any()))
        .thenReturn(true);
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

  private FilesComponent newComponent() {
    return newComponent(manifestLoaderFor(new File("devops").getAbsolutePath()));
  }

  private FilesComponent newComponent(SupportBundleV2ManifestLoader manifestLoader) {
    return new FilesComponent(
        mockNodeUniverseManager,
        new SupportBundleRunAsSupport(mockImageBundleUtil, manifestLoader),
        mockCgroupScriptExecutor,
        mockLogParseSupport,
        mockCdcStateDumpSupport,
        mockServerConfRedactionSupport,
        mockScriptEstimateSupport,
        mockConfig,
        mockConfGetter);
  }

  private static SupportBundleV2ManifestLoader manifestLoaderFor(String devopsHome) {
    return new SupportBundleV2ManifestLoader(
        ConfigFactory.parseString(String.format("yb.devops.home = \"%s\"", devopsHome)),
        CustomObjectMapperModule.createDefaultMapper());
  }

  private SupportBundleTaskParams paramsWithSpec(FilesComponentSpec... specs) {
    SupportBundleFormData bundleData = new SupportBundleFormData();
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setFilesComponentSpecs(Arrays.asList(specs));
    UUID bundleUuid = UUID.randomUUID();
    bundleUuids.add(bundleUuid);
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

  private FilesComponentSpec baseSpec() {
    FilesComponentSpec spec = new FilesComponentSpec();
    spec.setComponentName("UniverseLogs");
    spec.setScriptPath("bin/node_utils.sh");
    spec.setParams(
        Arrays.asList(
            "create_universe_logs_bundle", "--remote_tar_path", "/tmp/${nodeName}-files.tar.gz"));
    spec.setRemoteTarPath("/tmp/${nodeName}-files.tar.gz");
    spec.setLinuxUser("yugabyte");
    spec.setTimeoutSecs(60);
    return spec;
  }

  /** Answers the node's existence check for any path. */
  private void stubFileExists(boolean exists) {
    when(mockNodeUniverseManager.runCommand(
            any(), any(), argThat(FilesComponentTest::isFileExistsCheck), any(), eq(false)))
        .thenReturn(fileExistsOutput(exists));
  }

  private static boolean isFileExistsCheck(List<String> command) {
    return command != null
        && command.size() == 5
        && SupportBundleNodeStaging.FILE_EXISTS_SCRIPT.equals(command.get(2));
  }

  private static ShellResponse fileExistsOutput(boolean exists) {
    return ShellResponse.create(
        0, ShellResponse.RUN_COMMAND_OUTPUT_PREFIX + "\n" + (exists ? "1" : "0"));
  }

  private void mockCopyToProduceTar() throws Exception {
    org.mockito.Mockito.doAnswer(
            answer -> {
              String localTargetPath = answer.getArgument(3);
              Path targetTarGzPath = Paths.get(localTargetPath);
              Path tempFile =
                  Paths.get(createTempFile(fakeSourcePath, "collected.txt", "collected-content"));
              Files.createDirectories(targetTarGzPath.getParent());
              createTarGzipFiles(Arrays.asList(tempFile), targetTarGzPath);
              return null;
            })
        .when(mockNodeUniverseManager)
        .copyFileFromNode(any(), any(), any(), any(), any());
  }

  @Test
  public void testDownloadComponentBetweenDatesSuccess() throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(
            ShellResponse.create(0, "BUNDLE_STATUS=OK\nBUNDLE_PATH=/tmp/u-n1-files.tar.gz"));
    stubFileExists(true);
    mockCopyToProduceTar();

    FilesComponent component = newComponent();
    component.downloadComponentBetweenDates(
        paramsWithSpec(baseSpec()),
        customer,
        universe,
        Paths.get(fakeBundlePath),
        new Date(Long.MIN_VALUE),
        new Date(Long.MAX_VALUE),
        node);

    verify(mockNodeUniverseManager, times(1)).runScript(any(), any(), any(), any(), any());
    verify(mockNodeUniverseManager, times(1))
        .copyFileFromNode(any(), any(), eq("/tmp/u-n1-files.tar.gz"), any(), any());
    verify(mockNodeUniverseManager, times(1))
        .runCommand(
            any(),
            any(),
            eq(Arrays.asList("rm", "-f", "/tmp/u-n1-files.tar.gz")),
            any(),
            eq(false));
    // The intermediate tar should be removed and its content untarred into the bundle dir.
    assertTrue(new File(Paths.get(fakeBundlePath, "collected.txt").toString()).exists());
    assertTrue(!new File(Paths.get(fakeBundlePath, "UniverseLogs.tar.gz").toString()).exists());
  }

  /** On the storage path, outside the tree that gets zipped, and gone once untarred. */
  @Test
  public void testRemoteTarIsStagedOnTheStoragePathOutsideTheBundle() throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=OK"));
    stubFileExists(true);
    mockCopyToProduceTar();
    when(mockNodeUniverseManager.maybeUpgradeAndGetNodeAgent(any(), any()))
        .thenReturn(Optional.of(new NodeAgent()));
    SupportBundleTaskParams taskParams = paramsWithSpec(baseSpec());
    Path stagingRoot = SupportBundleStaging.rootFor(taskParams.supportBundle.getBundleUUID());

    newComponent()
        .downloadComponentBetweenDates(
            taskParams,
            customer,
            universe,
            Paths.get(fakeBundlePath),
            new Date(Long.MIN_VALUE),
            new Date(Long.MAX_VALUE),
            node);

    ArgumentCaptor<String> localTar = ArgumentCaptor.forClass(String.class);
    verify(mockNodeUniverseManager)
        .copyFileFromNode(any(), any(), any(), localTar.capture(), any());
    Path specDir = Paths.get(localTar.getValue()).getParent();
    assertEquals(stagingRoot, specDir.getParent());
    assertFalse(specDir.startsWith(Paths.get(fakeBundlePath)));
    assertFalse("the spec's staging directory is removed", Files.exists(specDir));
    assertTrue(new File(fakeBundlePath, "collected.txt").exists());
  }

  /**
   * Without node agent the copy lands in java.io.tmpdir: copy_file mkdirs its parent on the node.
   */
  @Test
  public void testWithoutNodeAgentTheTarIsCopiedViaTmpdir() throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=OK"));
    stubFileExists(true);
    mockCopyToProduceTar();

    newComponent()
        .downloadComponentBetweenDates(
            paramsWithSpec(baseSpec()),
            customer,
            universe,
            Paths.get(fakeBundlePath),
            new Date(Long.MIN_VALUE),
            new Date(Long.MAX_VALUE),
            node);

    ArgumentCaptor<String> localTar = ArgumentCaptor.forClass(String.class);
    verify(mockNodeUniverseManager)
        .copyFileFromNode(any(), any(), any(), localTar.capture(), any());
    Path copied = Paths.get(localTar.getValue());
    assertEquals(Paths.get(System.getProperty("java.io.tmpdir")), copied.getParent());
    assertFalse("the temporary copy is removed", Files.exists(copied));
    assertTrue(new File(fakeBundlePath, "collected.txt").exists());
  }

  /** A tar that fails to gunzip still leaves nothing in java.io.tmpdir. */
  @Test
  public void testWithoutNodeAgentACorruptTarLeavesNothingInTmpdir() throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=OK"));
    stubFileExists(true);
    org.mockito.Mockito.doAnswer(
            answer -> {
              Files.writeString(Paths.get((String) answer.getArgument(3)), "not a gzip stream");
              return null;
            })
        .when(mockNodeUniverseManager)
        .copyFileFromNode(any(), any(), any(), any(), any());

    collectBaseSpec();

    ArgumentCaptor<String> localTar = ArgumentCaptor.forClass(String.class);
    verify(mockNodeUniverseManager)
        .copyFileFromNode(any(), any(), any(), localTar.capture(), any());
    Path copied = Paths.get(localTar.getValue());
    assertEquals(Paths.get(System.getProperty("java.io.tmpdir")), copied.getParent());
    assertFalse("the temporary copy is removed", Files.exists(copied));
  }

  /** A node archive can't write outside the bundle, and the rest of it is still extracted. */
  @Test
  public void testEntryOutsideTheBundleIsNotWritten() throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=OK"));
    stubFileExists(true);
    org.mockito.Mockito.doAnswer(
            answer -> {
              Path localTar = Paths.get((String) answer.getArgument(3));
              createTarGzipWithEntries(
                  List.of(
                      new String[] {"../escaped.txt", "x"},
                      new String[] {"collected.txt", "collected-content"}),
                  localTar);
              return null;
            })
        .when(mockNodeUniverseManager)
        .copyFileFromNode(any(), any(), any(), any(), any());

    collectBaseSpec();

    assertFalse(Files.exists(Paths.get(fakeBundleBasePath, "escaped.txt")));
    assertTrue(Files.exists(Paths.get(fakeBundlePath, "collected.txt")));
  }

  /** A BUNDLE_PATH other than the requested path is never copied or deleted. */
  @Test
  public void testDifferentReportedBundlePathIsIgnored() throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=OK\nBUNDLE_PATH=/etc/passwd"));
    stubFileExists(true);
    mockCopyToProduceTar();

    newComponent()
        .downloadComponentBetweenDates(
            paramsWithSpec(baseSpec()),
            customer,
            universe,
            Paths.get(fakeBundlePath),
            new Date(Long.MIN_VALUE),
            new Date(Long.MAX_VALUE),
            node);

    verify(mockNodeUniverseManager)
        .copyFileFromNode(any(), any(), eq("/tmp/u-n1-files.tar.gz"), any(), any());
    verify(mockNodeUniverseManager)
        .runCommand(
            any(),
            any(),
            eq(Arrays.asList("rm", "-f", "/tmp/u-n1-files.tar.gz")),
            any(),
            eq(false));
    verify(mockNodeUniverseManager, never())
        .copyFileFromNode(any(), any(), eq("/etc/passwd"), any(), any());
  }

  @Test
  public void testNodeNameSubstitutionUsesRemoteTarPathWhenNoBundlePath() throws Exception {
    // No BUNDLE_PATH in stdout, so the substituted remoteTarPath must be used.
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=OK"));
    stubFileExists(true);
    mockCopyToProduceTar();

    FilesComponent component = newComponent();
    component.downloadComponentBetweenDates(
        paramsWithSpec(baseSpec()),
        customer,
        universe,
        Paths.get(fakeBundlePath),
        new Date(Long.MIN_VALUE),
        new Date(Long.MAX_VALUE),
        node);

    // ${nodeName} resolved to the node name in both check and copy.
    verify(mockNodeUniverseManager, times(1))
        .runCommand(
            any(),
            any(),
            eq(SupportBundleNodeStaging.fileExistsCommand("/tmp/u-n1-files.tar.gz")),
            any(),
            eq(false));
    verify(mockNodeUniverseManager, times(1))
        .copyFileFromNode(any(), any(), eq("/tmp/u-n1-files.tar.gz"), any(), any());
  }

  /** A failed run can still leave a partial tar, e.g. from a full disk or a timeout. */
  @Test
  public void testNonZeroExitRemovesTheRemoteTar() throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(ShellResponse.create(1, "tar: write error: No space left on device"));

    collectBaseSpec();

    verify(mockNodeUniverseManager, never()).copyFileFromNode(any(), any(), any(), any(), any());
    verifyRemoteTarRemoved();
  }

  @Test
  public void testFailedStatusRemovesTheRemoteTar() throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=FAILED"));

    collectBaseSpec();

    verify(mockNodeUniverseManager, never()).copyFileFromNode(any(), any(), any(), any(), any());
    verifyRemoteTarRemoved();
  }

  /** With a long spec timeout, so the rm's own bound shows. */
  private void collectBaseSpec() throws Exception {
    FilesComponentSpec spec = baseSpec();
    spec.setTimeoutSecs(1800);
    newComponent()
        .downloadComponentBetweenDates(
            paramsWithSpec(spec),
            customer,
            universe,
            Paths.get(fakeBundlePath),
            new Date(Long.MIN_VALUE),
            new Date(Long.MAX_VALUE),
            node);
  }

  /** Whatever the spec's timeout, the rm gets 60 s. */
  private void verifyRemoteTarRemoved() {
    ArgumentCaptor<ShellProcessContext> rmContext =
        ArgumentCaptor.forClass(ShellProcessContext.class);
    verify(mockNodeUniverseManager)
        .runCommand(
            any(),
            any(),
            eq(Arrays.asList("rm", "-f", "/tmp/u-n1-files.tar.gz")),
            rmContext.capture(),
            eq(false));
    assertEquals(60L, rmContext.getValue().getTimeoutSecs());
  }

  @Test
  public void testEmptyStatusSkipsCopy() throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=EMPTY"));

    FilesComponent component = newComponent();
    component.downloadComponentBetweenDates(
        paramsWithSpec(baseSpec()),
        customer,
        universe,
        Paths.get(fakeBundlePath),
        new Date(Long.MIN_VALUE),
        new Date(Long.MAX_VALUE),
        node);

    verify(mockNodeUniverseManager, never()).copyFileFromNode(any(), any(), any(), any());
  }

  @Test
  public void testEmptyStatusCopiesWhenRemoteTarExists() throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(
            ShellResponse.create(
                0,
                "BUNDLE_STATUS=EMPTY\nBUNDLE_PATH=/tmp/u-n1-files.tar.gz\n"
                    + "no matching rows on this node"));
    when(mockNodeUniverseManager.runCommand(
            any(),
            any(),
            eq(SupportBundleNodeStaging.fileExistsCommand("/tmp/u-n1-files.tar.gz")),
            any(),
            eq(false)))
        .thenReturn(fileExistsOutput(true));
    mockCopyToProduceTar();

    FilesComponent component = newComponent();
    component.downloadComponentBetweenDates(
        paramsWithSpec(baseSpec()),
        customer,
        universe,
        Paths.get(fakeBundlePath),
        new Date(Long.MIN_VALUE),
        new Date(Long.MAX_VALUE),
        node);

    verify(mockNodeUniverseManager, times(1))
        .copyFileFromNode(any(), any(), eq("/tmp/u-n1-files.tar.gz"), any(), any());
  }

  @Test
  public void testEmptyStatusSkipsCopyWhenTheNodeHasNoTar() throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(
            ShellResponse.create(0, "BUNDLE_STATUS=EMPTY\nBUNDLE_PATH=/tmp/u-n1-files.tar.gz\n"));
    stubFileExists(false);

    newComponent()
        .downloadComponentBetweenDates(
            paramsWithSpec(baseSpec()),
            customer,
            universe,
            Paths.get(fakeBundlePath),
            new Date(Long.MIN_VALUE),
            new Date(Long.MAX_VALUE),
            node);

    verify(mockNodeUniverseManager, never()).copyFileFromNode(any(), any(), any(), any(), any());
  }

  @Test
  public void testFilesComponentSpecSerializationRoundTrip() throws Exception {
    ObjectMapper mapper = new ObjectMapper();
    mapper.disable(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES);
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setFilesComponentSpecs(Collections.singletonList(baseSpec()));

    String json = mapper.writeValueAsString(bundleDetails);
    BundleDetails parsed = mapper.readValue(json, BundleDetails.class);

    assertNotNull(parsed.getFilesComponentSpecs());
    assertEquals(1, parsed.getFilesComponentSpecs().size());
    FilesComponentSpec parsedSpec = parsed.getFilesComponentSpecs().get(0);
    assertEquals("UniverseLogs", parsedSpec.getComponentName());
    assertEquals("bin/node_utils.sh", parsedSpec.getScriptPath());
    assertEquals("/tmp/${nodeName}-files.tar.gz", parsedSpec.getRemoteTarPath());
    assertEquals("yugabyte", parsedSpec.getLinuxUser());
    assertEquals(60L, parsedSpec.getTimeoutSecs());
    List<String> params = parsedSpec.getParams();
    assertEquals("create_universe_logs_bundle", params.get(0));
  }

  private void mockCopyToProduceServerConfTar(String serverConfContent) throws Exception {
    org.mockito.Mockito.doAnswer(
            answer -> {
              String localTargetPath = answer.getArgument(3);
              Path targetTarGzPath = Paths.get(localTargetPath);
              Files.createDirectories(targetTarGzPath.getParent());
              createTarGzipWithEntry("master/conf/server.conf", serverConfContent, targetTarGzPath);
              return null;
            })
        .when(mockNodeUniverseManager)
        .copyFileFromNode(any(), any(), any(), any(), any());
  }

  private static void createTarGzipWithEntry(String entryName, String content, Path output)
      throws IOException {
    createTarGzipWithEntries(Collections.singletonList(new String[] {entryName, content}), output);
  }

  /** Each entry is a {name, content} pair, written in order. */
  private static void createTarGzipWithEntries(List<String[]> entries, Path output)
      throws IOException {
    try (OutputStream fOut = Files.newOutputStream(output);
        BufferedOutputStream buffOut = new BufferedOutputStream(fOut);
        GzipCompressorOutputStream gzOut = new GzipCompressorOutputStream(buffOut);
        TarArchiveOutputStream tOut = new TarArchiveOutputStream(gzOut)) {
      for (String[] entry : entries) {
        byte[] bytes = entry[1].getBytes(StandardCharsets.UTF_8);
        TarArchiveEntry tarEntry = new TarArchiveEntry(entry[0]);
        tarEntry.setSize(bytes.length);
        tOut.putArchiveEntry(tarEntry);
        tOut.write(bytes);
        tOut.closeArchiveEntry();
      }
      tOut.finish();
    }
  }

  private FilesComponentSpec gFlagsSpec() {
    FilesComponentSpec spec = new FilesComponentSpec();
    spec.setComponentName("GFlags");
    spec.setScriptPath("bin/node_utils.sh");
    spec.setParams(
        Arrays.asList(
            "create_gflags_bundle", "--remote_tar_path", "/tmp/${nodeName}-gflags.tar.gz"));
    spec.setRemoteTarPath("/tmp/${nodeName}-gflags.tar.gz");
    spec.setLinuxUser("yugabyte");
    spec.setTimeoutSecs(60);
    return spec;
  }

  /** Runs the GFlags spec and returns the untarred server.conf contents. */
  private String collectGFlagsServerConf(String scriptStdout, String serverConfContent)
      throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(ShellResponse.create(0, scriptStdout));
    stubFileExists(true);
    mockCopyToProduceServerConfTar(serverConfContent);

    FilesComponent component = newComponent();
    component.downloadComponentBetweenDates(
        paramsWithSpec(gFlagsSpec()),
        customer,
        universe,
        Paths.get(fakeBundlePath),
        new Date(Long.MIN_VALUE),
        new Date(Long.MAX_VALUE),
        node);

    Path serverConf = Paths.get(fakeBundlePath, "master/conf/server.conf");
    assertTrue(Files.exists(serverConf));
    return new String(Files.readAllBytes(serverConf), StandardCharsets.UTF_8);
  }

  /**
   * Runs the GFlags spec with the given script result and returns the ERROR lines that start with
   * REDACTION_FAILURE. The tar is copied only when {@code expectCopy}, which keeps the strict
   * runner from flagging stubs a test never reaches.
   */
  private List<String> runGFlagsCapturingRedactionFailures(
      ShellResponse scriptResult, boolean expectCopy) throws Exception {
    return runCapturingRedactionFailures(gFlagsSpec(), scriptResult, expectCopy);
  }

  private List<String> runCapturingRedactionFailures(
      FilesComponentSpec spec, ShellResponse scriptResult, boolean expectCopy) throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(scriptResult);
    if (expectCopy) {
      stubFileExists(true);
      mockCopyToProduceServerConfTar("--ycql_ldap_bind_passwd=REDACTED");
    }
    Logger componentLogger = (Logger) LoggerFactory.getLogger(AbstractScriptTarNodeComponent.class);
    ListAppender<ILoggingEvent> appender = new ListAppender<>();
    appender.start();
    componentLogger.addAppender(appender);
    try {
      newComponent()
          .downloadComponentBetweenDates(
              paramsWithSpec(spec),
              customer,
              universe,
              Paths.get(fakeBundlePath),
              new Date(Long.MIN_VALUE),
              new Date(Long.MAX_VALUE),
              node);
    } finally {
      componentLogger.detachAppender(appender);
    }
    return appender.list.stream()
        .filter(event -> event.getLevel() == Level.ERROR)
        .map(ILoggingEvent::getFormattedMessage)
        .filter(message -> message.startsWith("REDACTION_FAILURE"))
        .collect(Collectors.toList());
  }

  @Test
  public void testGFlagsSpecCollectsWhatTheNodeRedacted() throws Exception {
    // YBA does no redaction of its own: what the node reports as redacted is extracted as-is.
    String contents =
        collectGFlagsServerConf(
            "BUNDLE_STATUS=OK\nBUNDLE_PATH=/tmp/u-n1-gflags.tar.gz\nREDACTION_STATUS=NODE",
            "--ycql_ldap_bind_passwd=REDACTED");

    assertEquals("--ycql_ldap_bind_passwd=REDACTED", contents);
    // With the bundle id, so the sensitive-gflag list is read once per bundle.
    verify(mockServerConfRedactionSupport, times(1))
        .appendRedactionScriptParams(notNull(), any(), any(), any());
    verify(mockServerConfRedactionSupport, never()).appendDropServerConfScriptParam(any());
  }

  @Test
  public void testGFlagsSpecCollectsWhenNothingToRedact() throws Exception {
    String contents =
        collectGFlagsServerConf(
            "BUNDLE_STATUS=OK\nBUNDLE_PATH=/tmp/u-n1-gflags.tar.gz"
                + "\nREDACTION_STATUS=NOT_APPLICABLE",
            "--webserver_port=13000");

    assertEquals("--webserver_port=13000", contents);
  }

  @Test
  public void testGFlagsSpecKeepsTarOnNodeWithoutRedactionOutcome() throws Exception {
    // Without a REDACTION_STATUS the tar may hold an unredacted server.conf, so it is not copied.
    List<String> errors =
        runGFlagsCapturingRedactionFailures(
            ShellResponse.create(0, "BUNDLE_STATUS=OK\nBUNDLE_PATH=/tmp/u-n1-gflags.tar.gz"),
            false);

    assertEquals(
        List.of(
            "REDACTION_FAILURE: component 'GFlags' on node 'u-n1' reported no redaction outcome"
                + " (BUNDLE_STATUS=OK); its server.conf is not collected"),
        errors);
    // Stops before even looking for the tar, so nothing past this point can copy it.
    verify(mockNodeUniverseManager, never())
        .runCommand(
            any(), any(), argThat(FilesComponentTest::isFileExistsCheck), any(), anyBoolean());
    verify(mockNodeUniverseManager, never()).copyFileFromNode(any(), any(), any(), any(), any());
    assertFalse(Files.exists(Paths.get(fakeBundlePath, "master/conf/server.conf")));
  }

  @Test
  public void testEmptyGFlagsWithNothingToRedactLogsNoRedactionFailure() throws Exception {
    // Nothing was staged, so there is no server.conf to protect.
    stubFileExists(false);

    List<String> errors =
        runGFlagsCapturingRedactionFailures(
            ShellResponse.create(0, "REDACTION_STATUS=NOT_APPLICABLE\nBUNDLE_STATUS=EMPTY"), false);

    assertEquals(List.of(), errors);
    verify(mockNodeUniverseManager, never()).copyFileFromNode(any(), any(), any(), any(), any());
  }

  @Test
  public void testEmptyGFlagsWithoutRedactionOutcomeIsStillAFailure() throws Exception {
    List<String> errors =
        runGFlagsCapturingRedactionFailures(ShellResponse.create(0, "BUNDLE_STATUS=EMPTY"), false);

    assertEquals(
        List.of(
            "REDACTION_FAILURE: component 'GFlags' on node 'u-n1' reported no redaction outcome"
                + " (BUNDLE_STATUS=EMPTY); its server.conf is not collected"),
        errors);
  }

  private FilesComponentSpec outputFilesSpec() {
    FilesComponentSpec spec = gFlagsSpec();
    spec.setComponentName("OutputFiles");
    spec.setParams(
        Arrays.asList(
            "create_output_files_bundle", "--remote_tar_path", "/tmp/${nodeName}-output.tar.gz"));
    spec.setRemoteTarPath("/tmp/${nodeName}-output.tar.gz");
    return spec;
  }

  /**
   * The tar goes in the bundle's directory under the node's tmp directory, which the bundle's
   * cleanup removes; the script, the copy and the per-spec rm all use that path.
   */
  @Test
  public void testTarGoesInTheBundlesDirectoryOnTheNode() throws Exception {
    String bundleDir = "/data/tmp/yba-support-bundle-b-fake_ip";
    when(mockCgroupScriptExecutor.stagingDir(any(), any(), any(), any())).thenReturn(bundleDir);
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=OK"));
    stubFileExists(true);
    mockCopyToProduceTar();

    newComponent()
        .downloadComponentBetweenDates(
            paramsWithSpec(baseSpec()),
            customer,
            universe,
            Paths.get(fakeBundlePath),
            new Date(Long.MIN_VALUE),
            new Date(Long.MAX_VALUE),
            node);

    String tarPath = bundleDir + "/u-n1-files.tar.gz";
    ArgumentCaptor<List<String>> params = ArgumentCaptor.forClass(List.class);
    verify(mockNodeUniverseManager).runScript(any(), any(), any(), params.capture(), any());
    assertEquals(
        tarPath, params.getValue().get(params.getValue().indexOf("--remote_tar_path") + 1));
    verify(mockNodeUniverseManager).copyFileFromNode(any(), any(), eq(tarPath), any(), any());
    verify(mockNodeUniverseManager)
        .runCommand(any(), any(), eq(Arrays.asList("rm", "-f", tarPath)), any(), eq(false));
  }

  /** A file the node couldn't copy is logged at WARN, and the rest of the tar is collected. */
  @Test
  public void testCopyFailuresAreLoggedAndTheRestCollected() throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(
            ShellResponse.create(
                0,
                "COPY_FAILURE=/home/yugabyte/tserver/tserver.out: cp: error reading"
                    + " '/home/yugabyte/tserver/tserver.out': Input/output error\n"
                    + "BUNDLE_STATUS=OK"));
    stubFileExists(true);
    mockCopyToProduceTar();
    Logger componentLogger = (Logger) LoggerFactory.getLogger(AbstractScriptTarNodeComponent.class);
    ListAppender<ILoggingEvent> appender = new ListAppender<>();
    appender.start();
    componentLogger.addAppender(appender);
    try {
      newComponent()
          .downloadComponentBetweenDates(
              paramsWithSpec(outputFilesSpec()),
              customer,
              universe,
              Paths.get(fakeBundlePath),
              new Date(Long.MIN_VALUE),
              new Date(Long.MAX_VALUE),
              node);
    } finally {
      componentLogger.detachAppender(appender);
    }

    assertEquals(
        List.of(
            "COPY_FAILURE on node 'u-n1' for component 'OutputFiles':"
                + " /home/yugabyte/tserver/tserver.out: cp: error reading"
                + " '/home/yugabyte/tserver/tserver.out': Input/output error"),
        appender.list.stream()
            .filter(event -> event.getLevel() == Level.WARN)
            .map(ILoggingEvent::getFormattedMessage)
            .filter(message -> message.startsWith("COPY_FAILURE"))
            .collect(Collectors.toList()));
    assertTrue(new File(Paths.get(fakeBundlePath, "collected.txt").toString()).exists());
  }

  @Test
  public void testUnmarkedFileListSpecGetsNoHelper() throws Exception {
    // OutputFiles isn't marked redactServerConf: no helper, no flags, and its tar is copied without
    // a redaction marker.
    List<String> errors =
        runCapturingRedactionFailures(
            outputFilesSpec(),
            ShellResponse.create(0, "BUNDLE_STATUS=OK\nBUNDLE_PATH=/tmp/u-n1-output.tar.gz"),
            true);

    assertEquals(List.of(), errors);
    verify(mockServerConfRedactionSupport, never())
        .ensureRedactionBinaryOnNode(any(), any(), any(), any());
    verify(mockServerConfRedactionSupport, never())
        .appendRedactionScriptParams(any(), any(), any(), any());
    verify(mockServerConfRedactionSupport, never()).appendDropServerConfScriptParam(any());
    verify(mockNodeUniverseManager, times(1)).copyFileFromNode(any(), any(), any(), any(), any());
  }

  @Test
  public void testServerConfDroppedByAnUnmarkedComponentIsLogged() throws Exception {
    // An unmarked component gets no helper, so the node drops a server.conf it stages; YBA still
    // logs that.
    stubFileExists(false);

    List<String> errors =
        runCapturingRedactionFailures(
            outputFilesSpec(),
            ShellResponse.create(
                0,
                "REDACTION_FAILURE=output_files/server.conf: this component doesn't redact"
                    + " server.conf (no --redact_bin); dropped from the bundle\n"
                    + "REDACTION_STATUS=FAILED\nBUNDLE_STATUS=EMPTY"),
            false);

    assertEquals(
        List.of(
            "REDACTION_FAILURE on node 'u-n1' for component 'OutputFiles':"
                + " output_files/server.conf: this component doesn't redact server.conf (no"
                + " --redact_bin);"
                + " dropped from the bundle"),
        errors);
    verify(mockServerConfRedactionSupport, never())
        .appendRedactionScriptParams(any(), any(), any(), any());
  }

  @Test
  public void testGFlagsSpecDropsOnNodeWithoutAVerifiedHelper() throws Exception {
    when(mockServerConfRedactionSupport.ensureRedactionBinaryOnNode(any(), any(), any(), any()))
        .thenReturn(false);
    stubFileExists(false);

    List<String> errors =
        runGFlagsCapturingRedactionFailures(
            ShellResponse.create(
                0,
                "REDACTION_FAILURE=master/conf/server.conf: redaction helper unavailable on this"
                    + " node; dropped from the bundle\nREDACTION_STATUS=FAILED\n"
                    + "BUNDLE_STATUS=EMPTY"),
            false);

    // YBA says why the node will drop it, then logs what the node dropped.
    assertEquals(2, errors.size());
    assertTrue(errors.get(0).contains("yb-redact-server-conf unavailable on node 'u-n1'"));
    assertEquals(
        "REDACTION_FAILURE on node 'u-n1' for component 'GFlags': master/conf/server.conf:"
            + " redaction helper unavailable on this node; dropped from the bundle",
        errors.get(1));
    ArgumentCaptor<List> paramsCaptor = ArgumentCaptor.forClass(List.class);
    verify(mockNodeUniverseManager).runScript(any(), any(), any(), paramsCaptor.capture(), any());
    assertTrue(paramsCaptor.getValue().contains(DROP_SERVER_CONF_PARAM));
    verify(mockServerConfRedactionSupport, never())
        .appendRedactionScriptParams(any(), any(), any(), any());
  }

  @Test
  public void testGFlagsSpecExtractsWhatTheNodeKept() throws Exception {
    // One file dropped on the node, the rest redacted: the tar is collected and the drop logged.
    List<String> errors =
        runGFlagsCapturingRedactionFailures(
            ShellResponse.create(
                0,
                "REDACTION_FAILURE=tserver/conf/server.conf: redaction helper failed (exit 3);"
                    + " dropped from the bundle\nREDACTION_STATUS=FAILED\nBUNDLE_STATUS=OK\n"
                    + "BUNDLE_PATH=/tmp/u-n1-gflags.tar.gz"),
            true);

    assertEquals(
        List.of(
            "REDACTION_FAILURE on node 'u-n1' for component 'GFlags': tserver/conf/server.conf:"
                + " redaction helper failed (exit 3); dropped from the bundle"),
        errors);
    assertTrue(Files.exists(Paths.get(fakeBundlePath, "master/conf/server.conf")));
  }

  @Test
  public void testRedactionFailureIsLoggedWhenTheComponentFails() throws Exception {
    // The script exits non-zero when it cannot delete a copy, so the line must be logged before
    // the exit code is checked.
    List<String> errors =
        runGFlagsCapturingRedactionFailures(
            ShellResponse.create(
                1,
                "REDACTION_FAILURE=master/conf/server.conf: redaction helper unavailable on this"
                    + " node; could not delete the unredacted copy, so nothing from this component"
                    + " is collected\nREDACTION_STATUS=FAILED\nBUNDLE_STATUS=FAILED"),
            false);

    assertEquals(1, errors.size());
    assertTrue(errors.get(0).contains("could not delete the unredacted copy"));
    verify(mockNodeUniverseManager, never()).copyFileFromNode(any(), any(), any(), any(), any());
  }

  @Test
  public void testGFlagsSpecLeavesNoArchiveWhenExtractionThrows() throws Exception {
    // The second entry cannot be written - its parent is the regular file the first entry just
    // created - so unTar throws part-way. No archive may be left for Util.zipAndDeleteDir to ship.
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(
            ShellResponse.create(
                0, "BUNDLE_STATUS=OK\nBUNDLE_PATH=/tmp/u-n1-gflags.tar.gz\nREDACTION_STATUS=NODE"));
    stubFileExists(true);
    org.mockito.Mockito.doAnswer(
            answer -> {
              Path targetTarGzPath = Paths.get((String) answer.getArgument(3));
              Files.createDirectories(targetTarGzPath.getParent());
              createTarGzipWithEntries(
                  Arrays.asList(
                      new String[] {"master/conf/server.conf", "--ycql_ldap_bind_passwd=REDACTED"},
                      new String[] {"master/conf/server.conf/unwritable", "boom"}),
                  targetTarGzPath);
              return null;
            })
        .when(mockNodeUniverseManager)
        .copyFileFromNode(any(), any(), any(), any(), any());

    FilesComponent component = newComponent();
    component.downloadComponentBetweenDates(
        paramsWithSpec(gFlagsSpec()),
        customer,
        universe,
        Paths.get(fakeBundlePath),
        new Date(Long.MIN_VALUE),
        new Date(Long.MAX_VALUE),
        node);

    assertEquals(Collections.emptyList(), archivesUnderBundle());
  }

  /** Every .tar/.tar.gz left inside the bundle directory. */
  private List<String> archivesUnderBundle() throws IOException {
    Path bundle = Paths.get(fakeBundlePath);
    if (!Files.exists(bundle)) {
      return Collections.emptyList();
    }
    try (Stream<Path> paths = Files.walk(bundle)) {
      return paths
          .filter(Files::isRegularFile)
          .map(path -> bundle.relativize(path).toString())
          .filter(name -> name.endsWith(".tar") || name.endsWith(".tar.gz"))
          .collect(Collectors.toList());
    }
  }

  @Test
  public void testNonRedactComponentNeverTouchesRedactionPath() throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(
            ShellResponse.create(0, "BUNDLE_STATUS=OK\nBUNDLE_PATH=/tmp/u-n1-files.tar.gz"));
    stubFileExists(true);
    mockCopyToProduceTar();

    FilesComponent component = newComponent();
    component.downloadComponentBetweenDates(
        paramsWithSpec(baseSpec()),
        customer,
        universe,
        Paths.get(fakeBundlePath),
        new Date(Long.MIN_VALUE),
        new Date(Long.MAX_VALUE),
        node);

    verify(mockServerConfRedactionSupport, never())
        .ensureRedactionBinaryOnNode(any(), any(), any(), any());
  }

  @Test
  public void testInterruptedThreadCollectsNothing() {
    Thread.currentThread().interrupt();

    assertThrows(
        CancellationException.class,
        () ->
            newComponent()
                .downloadComponentBetweenDates(
                    paramsWithSpec(baseSpec()),
                    customer,
                    universe,
                    Paths.get(fakeBundlePath),
                    new Date(Long.MIN_VALUE),
                    new Date(Long.MAX_VALUE),
                    node));

    assertTrue(Thread.interrupted());
    verify(mockNodeUniverseManager, never()).runScript(any(), any(), any(), any(), any());
  }

  /**
   * The tar is removed even though the cancel left the thread interrupted; the next spec isn't run.
   */
  @Test
  public void testCancelledRunRemovesItsTarAndStopsTheComponent() {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenAnswer(
            invocation -> {
              Thread.currentThread().interrupt();
              throw new CancellationException("cancelled");
            });
    boolean[] interruptedDuringRemove = {true};
    when(mockNodeUniverseManager.runCommand(
            any(),
            any(),
            eq(Arrays.asList("rm", "-f", "/tmp/u-n1-files.tar.gz")),
            any(),
            eq(false)))
        .thenAnswer(
            invocation -> {
              interruptedDuringRemove[0] = Thread.currentThread().isInterrupted();
              return ShellResponse.create(0, "");
            });
    FilesComponentSpec second = baseSpec();
    second.setComponentName("YbcLogs");

    assertThrows(
        CancellationException.class,
        () ->
            newComponent()
                .downloadComponentBetweenDates(
                    paramsWithSpec(baseSpec(), second),
                    customer,
                    universe,
                    Paths.get(fakeBundlePath),
                    new Date(Long.MIN_VALUE),
                    new Date(Long.MAX_VALUE),
                    node));

    assertTrue(Thread.interrupted());
    assertFalse(interruptedDuringRemove[0]);
    verify(mockNodeUniverseManager, times(1)).runScript(any(), any(), any(), any(), any());
  }

  @Test
  public void testSystemLogsInstallsHelperAsSpecUserAndRunsAsSshUser() throws Exception {
    // ec2-user cannot mkdir under /home/yugabyte, and the failed install would be cached for every
    // later spec on the node. The script keeps the ssh user it needs to read /var/log. The install
    // is keyed on the bundle, so the node is checked once per bundle.
    node.placementUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    when(mockImageBundleUtil.findEffectiveSshUser(any(), any(), any())).thenReturn("ec2-user");
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=EMPTY"));
    FilesComponentSpec spec = baseSpec();
    spec.setComponentName("SystemLogs");
    spec.setParams(Collections.emptyList());
    SupportBundleTaskParams params = paramsWithSpec(spec);

    newComponent()
        .downloadComponentBetweenDates(
            params,
            customer,
            universe,
            Paths.get(fakeBundlePath),
            new Date(Long.MIN_VALUE),
            new Date(Long.MAX_VALUE),
            node);

    ArgumentCaptor<ShellProcessContext> installContext =
        ArgumentCaptor.forClass(ShellProcessContext.class);
    verify(mockLogParseSupport)
        .ensureLogParseHelperOnNode(
            eq(params.supportBundle.getBundleUUID()),
            eq(node),
            eq(universe),
            installContext.capture());
    assertEquals("yugabyte", installContext.getValue().getSshUser());

    ArgumentCaptor<ShellProcessContext> runContext =
        ArgumentCaptor.forClass(ShellProcessContext.class);
    verify(mockNodeUniverseManager).runScript(any(), any(), any(), any(), runContext.capture());
    assertEquals("ec2-user", runContext.getValue().getSshUser());
    // ec2-user can't write the yugabyte-owned bundle directory, so its tar stays in /tmp.
    verify(mockCgroupScriptExecutor, never()).stagingDir(any(), any(), any(), any());
  }

  @Test
  public void testSpecWithoutATimeoutRunsWithTheScriptDefault() throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=EMPTY"));
    FilesComponentSpec spec = baseSpec();
    spec.setTimeoutSecs(0);

    newComponent()
        .downloadComponentBetweenDates(
            paramsWithSpec(spec),
            customer,
            universe,
            Paths.get(fakeBundlePath),
            new Date(Long.MIN_VALUE),
            new Date(Long.MAX_VALUE),
            node);

    ArgumentCaptor<ShellProcessContext> runContext =
        ArgumentCaptor.forClass(ShellProcessContext.class);
    verify(mockNodeUniverseManager).runScript(any(), any(), any(), any(), runContext.capture());
    assertEquals(3600L, runContext.getValue().getTimeoutSecs());
  }

  /** The script runs as runAs with the spec's timeout; setup runs as yugabyte, capped. */
  @Test
  public void testContextsForRunAsTheManifestsUserAndSetUpAsYugabyte() {
    node.placementUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    when(mockImageBundleUtil.findEffectiveSshUser(any(), any(), any())).thenReturn("ec2-user");
    SupportBundleRunAsSupport runAs =
        new SupportBundleRunAsSupport(
            mockImageBundleUtil, manifestLoaderFor(new File("devops").getAbsolutePath()));
    FilesComponentSpec spec = baseSpec();

    SupportBundleRunAsSupport.SpecContexts systemLogs =
        runAs.contextsFor(spec, "SystemLogs", universe, node, 3600L);
    assertEquals("ec2-user", systemLogs.run().getSshUser());
    assertEquals(3600L, systemLogs.run().getTimeoutSecs());
    assertEquals("yugabyte", systemLogs.setup().getSshUser());
    assertEquals(60L, systemLogs.setup().getTimeoutSecs());

    SupportBundleRunAsSupport.SpecContexts universeLogs =
        runAs.contextsFor(spec, "UniverseLogs", universe, node, 30L);
    assertEquals("yugabyte", universeLogs.run().getSshUser());
    assertEquals(30L, universeLogs.run().getTimeoutSecs());
    assertEquals("yugabyte", universeLogs.setup().getSshUser());
    assertEquals(30L, universeLogs.setup().getTimeoutSecs());
  }

  /** The run user follows runAs in the manifest, not the component name. */
  @Test
  public void testRunUserComesFromManifestNotComponentName() throws Exception {
    Path devopsHome = Paths.get(fakeBundleBasePath, "fixture-devops");
    Files.createDirectories(devopsHome.resolve("bin"));
    Files.writeString(
        devopsHome.resolve(SupportBundleV2ManifestLoader.MANIFEST_RELATIVE_PATH),
        String.join(
            "\n",
            "spec:",
            "  componentConfigs:",
            "    UniverseLogs:",
            "      runAs: providerSshUser",
            "    SystemLogs:",
            "      enabled: true",
            ""));
    node.placementUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    when(mockImageBundleUtil.findEffectiveSshUser(any(), any(), any())).thenReturn("ec2-user");
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=EMPTY"));
    FilesComponent component = newComponent(manifestLoaderFor(devopsHome.toString()));
    FilesComponentSpec systemLogs = baseSpec();
    systemLogs.setComponentName("SystemLogs");
    // Empty params skip param resolution, which needs runtime config.
    systemLogs.setParams(Collections.emptyList());

    for (FilesComponentSpec spec : List.of(baseSpec(), systemLogs)) {
      component.downloadComponentBetweenDates(
          paramsWithSpec(spec),
          customer,
          universe,
          Paths.get(fakeBundlePath),
          new Date(Long.MIN_VALUE),
          new Date(Long.MAX_VALUE),
          node);
    }

    ArgumentCaptor<ShellProcessContext> runContext =
        ArgumentCaptor.forClass(ShellProcessContext.class);
    verify(mockNodeUniverseManager, times(2))
        .runScript(any(), any(), any(), any(), runContext.capture());
    assertEquals("ec2-user", runContext.getAllValues().get(0).getSshUser());
    assertEquals("yugabyte", runContext.getAllValues().get(1).getSshUser());
  }

  /**
   * A task abort reaches the loop as an exception whose cause is the interrupt, the way node agent
   * wraps it; the loop stops at the running spec instead of moving on.
   */
  @Test
  public void testAbortStopsAtTheRunningSpec() throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenThrow(new RuntimeException(new InterruptedException("aborted")));
    FilesComponentSpec second = baseSpec();
    second.setComponentName("GFlags");
    SupportBundleTaskParams taskParams = paramsWithSpec(baseSpec(), second);

    FilesComponent component = newComponent();
    List<String> cancelled = new ArrayList<>();
    List<String> errors =
        logged(
            AbstractScriptTarNodeComponent.class,
            Level.ERROR,
            () ->
                cancelled.addAll(
                    logged(
                        SupportBundleCancellation.class,
                        Level.INFO,
                        () ->
                            assertThrows(
                                RuntimeException.class,
                                () ->
                                    component.downloadComponentBetweenDates(
                                        taskParams,
                                        customer,
                                        universe,
                                        Paths.get(fakeBundlePath),
                                        new Date(Long.MIN_VALUE),
                                        new Date(Long.MAX_VALUE),
                                        node)))));
    verify(mockNodeUniverseManager, times(1)).runScript(any(), any(), any(), any(), any());
    assertTrue(Thread.interrupted());
    // Removed before the abort propagates.
    verifyRemoteTarRemoved();
    // Logged as where the abort landed, not as a failed spec.
    assertEquals(
        List.of(
            "Support bundle work cancelled at FilesComponent spec 'UniverseLogs' on node 'u-n1'"),
        cancelled);
    assertTrue(errors.toString(), errors.stream().noneMatch(m -> m.startsWith("Error collecting")));
  }

  /** A spec that fails, rather than being cancelled, is an ERROR and logs no cancellation. */
  @Test
  public void testFailedSpecIsAnErrorNotACancellation() throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenThrow(new RuntimeException("script failed"));
    SupportBundleTaskParams taskParams = paramsWithSpec(baseSpec());

    List<String> cancelled = new ArrayList<>();
    List<String> errors =
        logged(
            AbstractScriptTarNodeComponent.class,
            Level.ERROR,
            () ->
                cancelled.addAll(
                    logged(
                        SupportBundleCancellation.class,
                        Level.INFO,
                        () ->
                            newComponent()
                                .downloadComponentBetweenDates(
                                    taskParams,
                                    customer,
                                    universe,
                                    Paths.get(fakeBundlePath),
                                    new Date(Long.MIN_VALUE),
                                    new Date(Long.MAX_VALUE),
                                    node))));

    assertTrue(cancelled.toString(), cancelled.isEmpty());
    assertTrue(
        errors.toString(),
        errors.stream()
            .anyMatch(
                m -> m.startsWith("Error collecting FilesComponent spec 'UniverseLogs' on node")));
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

  /** A tar the node fails to remove is logged at WARN with its path; nothing else removes it. */
  @Test
  public void testFailedTarRemovalIsLoggedAtWarn() throws Exception {
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any(), any()))
        .thenReturn(ShellResponse.create(0, "BUNDLE_STATUS=OK"));
    stubFileExists(true);
    mockCopyToProduceTar();
    when(mockNodeUniverseManager.runCommand(
            any(),
            any(),
            eq(Arrays.asList("rm", "-f", "/tmp/u-n1-files.tar.gz")),
            any(),
            eq(false)))
        .thenReturn(ShellResponse.create(1, "rm: cannot remove: Permission denied"));

    List<String> warnings =
        logged(
            AbstractScriptTarNodeComponent.class,
            Level.WARN,
            () ->
                newComponent()
                    .downloadComponentBetweenDates(
                        paramsWithSpec(baseSpec()),
                        customer,
                        universe,
                        Paths.get(fakeBundlePath),
                        new Date(Long.MIN_VALUE),
                        new Date(Long.MAX_VALUE),
                        node));

    assertEquals(
        List.of(
            "Could not remove support bundle tar '/tmp/u-n1-files.tar.gz' on node 'u-n1': rm:"
                + " cannot remove: Permission denied"),
        warnings);
  }

  /** The end of a bundle logs that its staging on YBA is gone. */
  @Test
  public void testBundleStagingRemovalIsLogged() throws Exception {
    UUID bundleUuid = UUID.randomUUID();
    bundleUuids.add(bundleUuid);
    Path root = SupportBundleStaging.rootFor(bundleUuid);
    Files.createDirectories(root.resolve("spec"));

    List<String> infos =
        logged(
            SupportBundleStaging.class,
            Level.INFO,
            () -> SupportBundleStaging.deleteFor(bundleUuid));

    assertFalse(Files.exists(root));
    assertEquals(List.of("Removed support bundle staging directory '" + root + "'"), infos);
    // Nothing staged on YBA: nothing to report, and no warning either.
    for (Level level : List.of(Level.INFO, Level.WARN)) {
      assertEquals(
          level.toString(),
          List.of(),
          logged(
              SupportBundleStaging.class, level, () -> SupportBundleStaging.deleteFor(bundleUuid)));
    }
  }
}
