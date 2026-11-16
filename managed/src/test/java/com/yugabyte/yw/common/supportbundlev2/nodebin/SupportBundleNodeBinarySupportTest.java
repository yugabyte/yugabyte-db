// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2.nodebin;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import ch.qos.logback.classic.Level;
import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.ConfigFactory;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ImageBundleUtil;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleCancellation;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleNodeStaging;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleRemoteChecksum;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleRunAsSupport;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleScriptEstimateSupport;
import com.yugabyte.yw.common.supportbundlev2.cgroup.SupportBundleCgroupScriptExecutor;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestLoader;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails.FilesComponentSpec;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.modules.CustomObjectMapperModule;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.HashSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import org.slf4j.LoggerFactory;
import play.Application;
import play.libs.Json;

/**
 * The helper binaries are refreshed on checksum, not merely on presence. A node left running an
 * older build of yb-redact-server-conf still reports {@code REDACTION_STATUS=NODE}, which the
 * collector trusts without re-checking, so a stale copy would yield a bundle that looks redacted
 * and is not.
 *
 * <p>That comparison only works if the check runs as a user who can read the copy. The create and
 * estimate paths both install and check as the spec's user through {@link
 * SupportBundleNodeBinarySupport#installContext}; the tests built on {@link
 * #stubNodeWhereOnlyYugabyteReachesYbHome} pin that.
 *
 * <p>The checksum itself must be read with plain argv. Support bundle commands go out with {@code
 * useBash=false}, so a shell pipeline in a single argv element reaches the program as literal
 * arguments and yields nothing -- which read as "checksums differ" and re-uploaded on every call,
 * on every spec, on every node.
 */
@RunWith(MockitoJUnitRunner.class)
public class SupportBundleNodeBinarySupportTest extends FakeDBApplication {

  private static final String BINARY = "yb-log-parse";
  private static final String REMOTE_PATH = "/home/yugabyte/bin/yb-log-parse";
  // Deliberately has no local file under devopsHome/bin, so there is nothing to verify against.
  private static final String UNBUILT_BINARY = "yb-unbuilt-helper";
  private static final String UNBUILT_REMOTE_PATH = "/home/yugabyte/bin/yb-unbuilt-helper";
  private static final String CHECKSUM_REMOTE_PATH = "/home/yugabyte/bin/yb-checksum-target.sh";
  private static final String MD5 = "fa4f9af3e7102854a4fe572eb2acfbb2";

  private static Path devopsHome;
  private static Path localBinary;

  @Mock private NodeUniverseManager mockNodeUniverseManager;
  @Mock private GFlagsValidation mockGFlagsValidation;

  private SupportBundleNodeBinarySupport binarySupport;
  private SupportBundleCdcStateDumpSupport cdcStateSupport;
  private SupportBundleServerConfRedactionSupport redactionSupport;
  private Universe universe;
  private final NodeDetails node = new NodeDetails();
  private String nodeCopyMd5;
  private String uploadedTmpMd5;

  @Override
  protected Application provideApplication() {
    try {
      devopsHome = Files.createTempDirectory("yba-devops-home");
      Files.createDirectories(devopsHome.resolve("bin"));
      localBinary = devopsHome.resolve("bin").resolve(BINARY + "-linux-amd64");
      Files.write(localBinary, "fake-helper-binary".getBytes(StandardCharsets.UTF_8));
      // The other two helpers get a local build as well, so their upload paths run on every
      // machine rather than only where the real devops/bin happens to hold them.
      for (String helper : Arrays.asList("yb-redact-server-conf", "yb-dump-cdc-state")) {
        Files.write(
            devopsHome.resolve("bin").resolve(helper + "-linux-amd64"),
            ("fake-" + helper).getBytes(StandardCharsets.UTF_8));
      }
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
    return provideApplication(ImmutableMap.of("yb.devops.home", devopsHome.toString()));
  }

  /** The test's devops home holds only fake helpers; the manifest is read from the real one. */
  private static SupportBundleV2ManifestLoader shippedManifestLoader() {
    return new SupportBundleV2ManifestLoader(
        ConfigFactory.parseString(
            String.format("yb.devops.home = \"%s\"", new File("devops").getAbsolutePath())),
        CustomObjectMapperModule.createDefaultMapper());
  }

  @Before
  public void setUp() {
    binarySupport = new SupportBundleNodeBinarySupport(mockNodeUniverseManager);
    // A real binary-support over the mocked NodeUniverseManager, so the deployment logic under
    // test is the production one rather than a stub of it.
    cdcStateSupport =
        new SupportBundleCdcStateDumpSupport(
            new SupportBundleNodeBinarySupport(mockNodeUniverseManager));
    redactionSupport =
        new SupportBundleServerConfRedactionSupport(
            new SupportBundleNodeBinarySupport(mockNodeUniverseManager),
            mockGFlagsValidation,
            shippedManifestLoader());
    Customer customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());
    node.nodeName = "u-n1";
    // NodeDetails.toString() dereferences cloudInfo, and Mockito calls it when building a
    // verification failure message -- without this a real failure surfaces as an NPE in the
    // reporter instead of the assertion that actually fired.
    node.cloudInfo = new CloudSpecificInfo();
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> u.getUniverseDetails().nodeDetailsSet = new HashSet<>(Arrays.asList(node)));

    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/home/yugabyte");
    stubNodeArch("x86_64");
  }

  /** Arch detection: uname -m on the node. A later stub in a test replaces the one from setUp. */
  private void stubNodeArch(String unameOutput) {
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            eq(Arrays.asList("uname", "-m")),
            any(ShellProcessContext.class)))
        .thenReturn(runCommandOutput(unameOutput));
  }

  private static boolean isFileExistsCheck(List<String> command) {
    return command != null
        && command.size() == 5
        && command
            .subList(0, 3)
            .equals(SupportBundleNodeStaging.fileExistsCommand("").subList(0, 3));
  }

  private static ShellResponse fileExistsOutput(boolean exists) {
    return ShellResponse.create(
        0, ShellResponse.RUN_COMMAND_OUTPUT_PREFIX + "\n" + (exists ? "1" : "0"));
  }

  @Test
  public void testSkipsUploadWhenNodeCopyMatches() throws Exception {
    stubRemoteChecksum(localChecksum());

    assertTrue(
        binarySupport.ensureBinaryOnNode(
            null,
            BINARY,
            node,
            universe,
            ShellProcessContext.builder().build(),
            "consequence",
            SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST));

    verify(mockNodeUniverseManager, never())
        .uploadFileToNode(any(), any(), any(), any(), any(), any());
  }

  @Test
  public void testReplacesStaleNodeCopy() {
    // A node carrying a different build is the case presence checks alone would miss.
    stubRemoteChecksum("0123456789abcdef0123456789abcdef");
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            eq(SupportBundleNodeStaging.fileExistsCommand(REMOTE_PATH)),
            any(ShellProcessContext.class),
            eq(false)))
        .thenReturn(fileExistsOutput(true));
    stubMkdir();
    stubRename();

    assertTrue(
        binarySupport.ensureBinaryOnNode(
            null,
            BINARY,
            node,
            universe,
            ShellProcessContext.builder().build(),
            "consequence",
            SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST));

    verifyUploadedThenRenamed(localBinary.toString(), REMOTE_PATH);
  }

  @Test
  public void testUploadThatFailsVerificationIsNotUsed() {
    stubRemoteChecksum("");
    stubMkdir();
    stubRename();
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            eq(SupportBundleNodeStaging.fileExistsCommand(REMOTE_PATH)),
            any(ShellProcessContext.class),
            eq(false)))
        .thenReturn(fileExistsOutput(false));

    assertFalse(deployBinary());
  }

  /** The live copy is never written, so a failed upload leaves it as it was. */
  @Test
  public void testFailedUploadRemovesTheTempAndNeverRenames() {
    stubRemoteChecksum("");
    stubMkdir();
    org.mockito.Mockito.doThrow(new RuntimeException("connection reset"))
        .when(mockNodeUniverseManager)
        .uploadFileToNode(any(), any(), any(), any(), any(), any());

    assertFalse(deployBinary());

    ArgumentCaptor<String> tmpPath = ArgumentCaptor.forClass(String.class);
    verify(mockNodeUniverseManager)
        .uploadFileToNode(any(), any(), any(), tmpPath.capture(), any(), any());
    verify(mockNodeUniverseManager)
        .runCommand(
            eq(node),
            eq(universe),
            eq(Arrays.asList("rm", "-f", tmpPath.getValue())),
            any(ShellProcessContext.class),
            eq(false));
    verify(mockNodeUniverseManager, never())
        .runCommand(
            any(),
            any(),
            argThat(cmd -> cmd != null && !cmd.isEmpty() && "mv".equals(cmd.get(0))),
            any(ShellProcessContext.class),
            eq(false));
  }

  @Test
  public void testFailedRenameRemovesTheTemp() {
    stubRemoteChecksum("");
    stubMkdir();
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            argThat(cmd -> cmd != null && !cmd.isEmpty() && "mv".equals(cmd.get(0))),
            any(ShellProcessContext.class),
            eq(false)))
        .thenReturn(ShellResponse.create(1, "mv: No space left on device"));

    assertFalse(deployBinary());

    ArgumentCaptor<String> tmpPath = ArgumentCaptor.forClass(String.class);
    verify(mockNodeUniverseManager)
        .uploadFileToNode(any(), any(), any(), tmpPath.capture(), any(), any());
    verify(mockNodeUniverseManager)
        .runCommand(
            eq(node),
            eq(universe),
            eq(Arrays.asList("rm", "-f", tmpPath.getValue())),
            any(ShellProcessContext.class),
            eq(false));
  }

  private boolean deployBinary() {
    return binarySupport.ensureBinaryOnNode(
        null,
        BINARY,
        node,
        universe,
        ShellProcessContext.builder().build(),
        "consequence",
        SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST);
  }

  @Test
  public void testUploadsWhenAbsentFromNode() {
    // A file md5sum cannot read is a non-zero exit, which reads as an unknown checksum.
    stubRemoteChecksum("");
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            eq(SupportBundleNodeStaging.fileExistsCommand(REMOTE_PATH)),
            any(ShellProcessContext.class),
            eq(false)))
        .thenReturn(fileExistsOutput(true));
    stubMkdir();
    stubRename();

    assertTrue(
        binarySupport.ensureBinaryOnNode(
            null,
            BINARY,
            node,
            universe,
            ShellProcessContext.builder().build(),
            "consequence",
            SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST));

    verifyUploadedThenRenamed(localBinary.toString(), REMOTE_PATH);
  }

  @Test
  public void testRefusesUnverifiedNodeCopyWhenPolicyIsReject() {
    // Nothing local to compare against. The redactor must refuse: a node running older redaction
    // logic still reports REDACTION_STATUS=NODE, so trusting it ships a bundle that looks redacted
    // and is not. It must not even consult the node -- the answer cannot change the outcome.
    assertFalse(
        binarySupport.ensureBinaryOnNode(
            null,
            UNBUILT_BINARY,
            node,
            universe,
            ShellProcessContext.builder().build(),
            "consequence",
            SupportBundleNodeBinarySupport.UnverifiedNodeCopy.REJECT));

    verify(mockNodeUniverseManager, never())
        .runCommand(
            eq(node),
            eq(universe),
            eq(SupportBundleNodeStaging.fileExistsCommand(UNBUILT_REMOTE_PATH)),
            any(ShellProcessContext.class),
            eq(false));
    verify(mockNodeUniverseManager, never())
        .uploadFileToNode(any(), any(), any(), any(), any(), any());
  }

  @Test
  public void testTrustsUnverifiedNodeCopyWhenPolicyAllows() {
    // The other two helpers keep the old behaviour: a stale build costs accuracy, and refusing
    // would fail collect_cdc_state outright rather than protect anything.
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            eq(SupportBundleNodeStaging.fileExistsCommand(UNBUILT_REMOTE_PATH)),
            any(ShellProcessContext.class),
            eq(false)))
        .thenReturn(fileExistsOutput(true));

    assertTrue(
        binarySupport.ensureBinaryOnNode(
            null,
            UNBUILT_BINARY,
            node,
            universe,
            ShellProcessContext.builder().build(),
            "consequence",
            SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST));
  }

  @Test
  public void testExistenceChecksRunUnderTheGivenContext() {
    // The 3-arg overload runs as the default user, which can report a present binary as missing
    // when the yb home is not traversable by that user.
    ShellProcessContext context = ShellProcessContext.builder().sshUser("centos").build();
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            eq(SupportBundleNodeStaging.fileExistsCommand(UNBUILT_REMOTE_PATH)),
            eq(context),
            eq(false)))
        .thenReturn(fileExistsOutput(true));

    assertTrue(
        binarySupport.ensureBinaryOnNode(
            null,
            UNBUILT_BINARY,
            node,
            universe,
            context,
            "consequence",
            SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST));

    verify(mockNodeUniverseManager)
        .runCommand(
            eq(node),
            eq(universe),
            eq(SupportBundleNodeStaging.fileExistsCommand(UNBUILT_REMOTE_PATH)),
            eq(context),
            eq(false));
  }

  @Test
  public void testDeploymentDecisionIsMadeOncePerBundleAndNode() throws Exception {
    // Every component on a node used to repeat the same uname -m and md5sum. With ~17 node
    // components that is ~34 round-trips per node where two suffice.
    stubRemoteChecksum(localChecksum());
    UUID bundle = UUID.randomUUID();
    ShellProcessContext context = ShellProcessContext.builder().build();

    for (int i = 0; i < 5; i++) {
      assertTrue(
          binarySupport.ensureBinaryOnNode(
              bundle,
              BINARY,
              node,
              universe,
              context,
              "consequence",
              SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST));
    }

    verify(mockNodeUniverseManager, times(1))
        .runCommand(
            eq(node),
            eq(universe),
            eq(Arrays.asList("uname", "-m")),
            any(ShellProcessContext.class));

    // A second bundle must decide for itself; the cache is scoped to one collection.
    assertTrue(
        binarySupport.ensureBinaryOnNode(
            UUID.randomUUID(),
            BINARY,
            node,
            universe,
            context,
            "consequence",
            SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST));
    verify(mockNodeUniverseManager, times(2))
        .runCommand(
            eq(node),
            eq(universe),
            eq(Arrays.asList("uname", "-m")),
            any(ShellProcessContext.class));
  }

  /** A cancelled step aborts the deploy; it isn't cached as "not deployed" for the bundle. */
  @Test
  public void testCancelledDeployThrowsAndIsNotCached() throws Exception {
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            eq(Arrays.asList("uname", "-m")),
            any(ShellProcessContext.class)))
        .thenReturn(cancelledResponse(), runCommandOutput("x86_64"));
    stubRemoteChecksum(localChecksum());
    UUID bundle = UUID.randomUUID();
    ShellProcessContext context = ShellProcessContext.builder().build();

    assertThrows(
        CancellationException.class,
        () ->
            binarySupport.ensureBinaryOnNode(
                bundle,
                BINARY,
                node,
                universe,
                context,
                "consequence",
                SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST));
    assertTrue(Thread.interrupted());

    assertTrue(
        binarySupport.ensureBinaryOnNode(
            bundle,
            BINARY,
            node,
            universe,
            context,
            "consequence",
            SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST));
  }

  /**
   * A deploy hung on one node holds no lock another node's deploy needs, even when their keys share
   * a bin of the map; the second node's name is chosen so they do.
   */
  @Test
  public void testHungDeployDoesNotBlockAnotherNodeInTheSameBin() throws Exception {
    UUID bundle = UUID.fromString("00000000-0000-0000-0000-000000000001");
    NodeDetails other = new NodeDetails();
    other.cloudInfo = new CloudSpecificInfo();
    other.nodeName = sameBinNodeName(bundle, node.nodeName);
    when(mockNodeUniverseManager.getYbHomeDir(other, universe)).thenReturn("/home/yugabyte");
    CountDownLatch hung = new CountDownLatch(1);
    CountDownLatch release = new CountDownLatch(1);
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            eq(Arrays.asList("uname", "-m")),
            any(ShellProcessContext.class)))
        .thenAnswer(
            invocation -> {
              hung.countDown();
              release.await();
              return runCommandOutput("x86_64");
            });
    when(mockNodeUniverseManager.runCommand(
            eq(other),
            eq(universe),
            eq(Arrays.asList("uname", "-m")),
            any(ShellProcessContext.class)))
        .thenReturn(runCommandOutput("x86_64"));
    stubRemoteChecksum(localChecksum());
    when(mockNodeUniverseManager.runCommand(
            eq(other),
            eq(universe),
            argThat(cmd -> cmd != null && cmd.size() == 2 && "md5sum".equals(cmd.get(0))),
            any(ShellProcessContext.class),
            eq(false)))
        .thenReturn(runCommandOutput(localChecksum()));
    ShellProcessContext context = ShellProcessContext.builder().build();
    ExecutorService pool = Executors.newFixedThreadPool(2);
    try {
      Future<Boolean> hungDeploy =
          pool.submit(
              () ->
                  binarySupport.ensureBinaryOnNode(
                      bundle,
                      BINARY,
                      node,
                      universe,
                      context,
                      "consequence",
                      SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST));
      assertTrue(hung.await(10, TimeUnit.SECONDS));

      Future<Boolean> otherDeploy =
          pool.submit(
              () ->
                  binarySupport.ensureBinaryOnNode(
                      bundle,
                      BINARY,
                      other,
                      universe,
                      context,
                      "consequence",
                      SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST));
      assertTrue(otherDeploy.get(10, TimeUnit.SECONDS));

      release.countDown();
      assertTrue(hungDeploy.get(10, TimeUnit.SECONDS));
    } finally {
      release.countDown();
      pool.shutdownNow();
    }
  }

  /** Two callers for one key deploy once: the second waits for the first's answer. */
  @Test
  public void testConcurrentCallersForOneKeyDeployOnce() throws Exception {
    UUID bundle = UUID.randomUUID();
    CountDownLatch hung = new CountDownLatch(1);
    CountDownLatch release = new CountDownLatch(1);
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            eq(Arrays.asList("uname", "-m")),
            any(ShellProcessContext.class)))
        .thenAnswer(
            invocation -> {
              hung.countDown();
              release.await();
              return runCommandOutput("x86_64");
            });
    stubRemoteChecksum(localChecksum());
    ShellProcessContext context = ShellProcessContext.builder().build();
    ExecutorService pool = Executors.newFixedThreadPool(2);
    try {
      List<Future<Boolean>> deploys = new ArrayList<>();
      for (int i = 0; i < 2; i++) {
        deploys.add(
            pool.submit(
                () ->
                    binarySupport.ensureBinaryOnNode(
                        bundle,
                        BINARY,
                        node,
                        universe,
                        context,
                        "consequence",
                        SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST)));
        if (i == 0) {
          assertTrue(hung.await(10, TimeUnit.SECONDS));
        }
      }
      assertThrows(TimeoutException.class, () -> deploys.get(1).get(500, TimeUnit.MILLISECONDS));

      release.countDown();
      assertTrue(deploys.get(0).get(10, TimeUnit.SECONDS));
      assertTrue(deploys.get(1).get(10, TimeUnit.SECONDS));
    } finally {
      release.countDown();
      pool.shutdownNow();
    }
    verify(mockNodeUniverseManager, times(1))
        .runCommand(
            eq(node),
            eq(universe),
            eq(Arrays.asList("uname", "-m")),
            any(ShellProcessContext.class));
  }

  /** A node name whose deploy key lands in the same bin as {@code nodeName}'s in a 16-bin map. */
  private static String sameBinNodeName(UUID bundle, String nodeName) {
    int bin = mapBin(bundle + "|" + nodeName + "|" + BINARY);
    for (int i = 2; ; i++) {
      String candidate = "u-n" + i;
      if (mapBin(bundle + "|" + candidate + "|" + BINARY) == bin) {
        return candidate;
      }
    }
  }

  // ConcurrentHashMap's spread() and bin index, for the 16 bins a new map starts with.
  private static int mapBin(String key) {
    int h = key.hashCode();
    return ((h ^ (h >>> 16)) & 0x7fffffff) & 15;
  }

  /** Without a YBA build the node's copy is checked for; a cancelled check is still an abort. */
  @Test
  public void testCancelledCheckOfAnUnbuiltHelperThrows() {
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            eq(SupportBundleNodeStaging.fileExistsCommand(UNBUILT_REMOTE_PATH)),
            any(ShellProcessContext.class),
            eq(false)))
        .thenReturn(cancelledResponse());

    assertThrows(
        CancellationException.class,
        () ->
            binarySupport.ensureBinaryOnNode(
                null,
                UNBUILT_BINARY,
                node,
                universe,
                ShellProcessContext.builder().build(),
                "consequence",
                SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST));
    assertTrue(Thread.interrupted());
  }

  @Test
  public void testCancelledChecksumThrows() {
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            argThat(cmd -> cmd != null && cmd.size() == 2 && "md5sum".equals(cmd.get(0))),
            any(ShellProcessContext.class),
            eq(false)))
        .thenReturn(cancelledResponse());

    assertThrows(
        CancellationException.class,
        () ->
            binarySupport.ensureBinaryOnNode(
                UUID.randomUUID(),
                BINARY,
                node,
                universe,
                ShellProcessContext.builder().build(),
                "consequence",
                SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST));
    assertTrue(Thread.interrupted());
    verify(mockNodeUniverseManager, never())
        .uploadFileToNode(any(), any(), any(), any(), any(), any());
  }

  @Test
  public void testForgetBundleReleasesTheDecision() throws Exception {
    stubRemoteChecksum(localChecksum());
    UUID bundle = UUID.randomUUID();
    ShellProcessContext context = ShellProcessContext.builder().build();

    binarySupport.ensureBinaryOnNode(
        bundle,
        BINARY,
        node,
        universe,
        context,
        "consequence",
        SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST);
    binarySupport.forgetBundle(bundle);
    binarySupport.ensureBinaryOnNode(
        bundle,
        BINARY,
        node,
        universe,
        context,
        "consequence",
        SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST);

    verify(mockNodeUniverseManager, times(2))
        .runCommand(
            eq(node),
            eq(universe),
            eq(Arrays.asList("uname", "-m")),
            any(ShellProcessContext.class));
  }

  @Test
  public void testLaterBundleOnlyChecksTheCopyAnEarlierOneInstalled() throws Exception {
    // SystemLogs runs as ec2-user. If a bundle installed or checked as that user, the upload would
    // fail, or the next bundle's checksum would be denied and read as "needs a sync".
    stubNodeWhereOnlyYugabyteReachesYbHome();

    ShellProcessContext systemLogsContext =
        ShellProcessContext.builder().sshUser("ec2-user").useDefaultUser(false).build();
    ShellProcessContext install =
        SupportBundleNodeBinarySupport.installContext(systemLogsContext, "yugabyte");

    assertTrue(
        binarySupport.ensureBinaryOnNode(
            UUID.randomUUID(),
            BINARY,
            node,
            universe,
            install,
            "consequence",
            SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST));
    // A later bundle, under its own UUID.
    assertTrue(
        binarySupport.ensureBinaryOnNode(
            UUID.randomUUID(),
            BINARY,
            node,
            universe,
            install,
            "consequence",
            SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST));

    verify(mockNodeUniverseManager, times(1))
        .uploadFileToNode(any(), any(), any(), any(), any(), any());
    verify(mockNodeUniverseManager, times(2))
        .runCommand(
            eq(node),
            eq(universe),
            argThat(cmd -> cmd != null && cmd.size() == 2 && "md5sum".equals(cmd.get(0))),
            any(ShellProcessContext.class),
            eq(false));
  }

  /** A spec without a user still installs helpers as yugabyte. */
  @Test
  public void testInstallContextDefaultsToYugabyte() {
    ShellProcessContext runContext = ShellProcessContext.builder().sshUser("ec2-user").build();
    assertEquals(
        "yugabyte", SupportBundleNodeBinarySupport.installContext(runContext, null).getSshUser());
    assertEquals(
        "yugabyte", SupportBundleNodeBinarySupport.installContext(runContext, " ").getSshUser());
    assertEquals(
        "yugabyte",
        SupportBundleNodeBinarySupport.installContext(runContext, "yugabyte").getSshUser());
  }

  /** A helper check never waits out a script's hour-long timeout, but keeps a shorter one. */
  @Test
  public void testInstallContextCapsTheTimeout() {
    assertEquals(
        60L,
        SupportBundleNodeBinarySupport.installContext(
                ShellProcessContext.builder().timeoutSecs(3600L).build(), null)
            .getTimeoutSecs());
    assertEquals(
        30L,
        SupportBundleNodeBinarySupport.installContext(
                ShellProcessContext.builder().timeoutSecs(30L).build(), null)
            .getTimeoutSecs());
    // No timeout on the run context means none was set, not "wait forever".
    assertEquals(
        60L,
        SupportBundleNodeBinarySupport.installContext(ShellProcessContext.builder().build(), null)
            .getTimeoutSecs());
  }

  @Test
  public void testSystemLogsEstimateRunsAsSshUserAndInstallsNothing() throws Exception {
    // Drives the real estimate path. The script keeps the ssh user it needs to read /var/log, is
    // staged as yugabyte, and no helper is installed on the node.
    node.cloudInfo.private_ip = "10.0.0.1";
    // Lets the provider ssh user resolve; without it SystemLogs quietly runs as the default user.
    node.placementUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    ImageBundleUtil imageBundleUtil = mock(ImageBundleUtil.class);
    when(imageBundleUtil.findEffectiveSshUser(any(), any(), any())).thenReturn("ec2-user");
    SupportBundleCgroupScriptExecutor scriptExecutor =
        mock(SupportBundleCgroupScriptExecutor.class);
    when(scriptExecutor.executeScript(
            any(), any(), any(), any(), any(), any(), any(), any(), any(), eq(false)))
        .thenReturn(ShellResponse.create(0, ""));
    RuntimeConfGetter confGetter = mock(RuntimeConfGetter.class);
    when(confGetter.getGlobalConf(GlobalConfKeys.supportBundleEstimateScriptTimeout))
        .thenReturn(Duration.ofMinutes(2));
    SupportBundleScriptEstimateSupport estimateSupport =
        new SupportBundleScriptEstimateSupport(
            app.config(),
            confGetter,
            mockNodeUniverseManager,
            new SupportBundleRunAsSupport(imageBundleUtil, shippedManifestLoader()),
            scriptExecutor,
            mockShellProcessHandler);
    FilesComponentSpec spec = new FilesComponentSpec();
    spec.setComponentName("SystemLogs");
    spec.setScriptPath("bin/support_bundle_utils.sh");
    spec.setLinuxUser("yugabyte");

    estimateSupport.runNodeScriptEstimate(
        spec,
        "SystemLogs",
        new SupportBundleFormData(),
        universe,
        node,
        null,
        null,
        UUID.randomUUID());

    verify(mockNodeUniverseManager, never())
        .uploadFileToNode(any(), any(), any(), any(), any(), any());
    ArgumentCaptor<ShellProcessContext> runContext =
        ArgumentCaptor.forClass(ShellProcessContext.class);
    ArgumentCaptor<ShellProcessContext> stageContext =
        ArgumentCaptor.forClass(ShellProcessContext.class);
    verify(scriptExecutor)
        .executeScript(
            any(),
            any(),
            any(),
            any(),
            any(),
            any(),
            runContext.capture(),
            stageContext.capture(),
            any(),
            eq(false));
    assertEquals("ec2-user", runContext.getValue().getSshUser());
    // The script is staged as yugabyte, like the helper; ec2-user only reads it.
    assertEquals("yugabyte", stageContext.getValue().getSshUser());
    // The spec's 3600 s default is capped by the 2 minute estimate script timeout.
    assertEquals(120L, runContext.getValue().getTimeoutSecs());

    // A sub-second cap still gives the script 1 s: 0 would mean no timeout at all.
    when(confGetter.getGlobalConf(GlobalConfKeys.supportBundleEstimateScriptTimeout))
        .thenReturn(Duration.ofMillis(500));
    estimateSupport.runNodeScriptEstimate(
        spec,
        "SystemLogs",
        new SupportBundleFormData(),
        universe,
        node,
        null,
        null,
        UUID.randomUUID());
    ArgumentCaptor<ShellProcessContext> laterRuns =
        ArgumentCaptor.forClass(ShellProcessContext.class);
    verify(scriptExecutor, times(2))
        .executeScript(
            any(), any(), any(), any(), any(), any(), laterRuns.capture(), any(), any(), eq(false));
    assertEquals(1L, laterRuns.getValue().getTimeoutSecs());
  }

  /** A cancelled estimate script is an abort, not a size of 0 the estimate moves past. */
  @Test
  public void testEstimatesRethrowACancelledScript() throws Exception {
    node.cloudInfo.private_ip = "10.0.0.1";
    node.placementUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    ImageBundleUtil imageBundleUtil = mock(ImageBundleUtil.class);
    when(imageBundleUtil.findEffectiveSshUser(any(), any(), any())).thenReturn("ec2-user");
    SupportBundleCgroupScriptExecutor scriptExecutor =
        mock(SupportBundleCgroupScriptExecutor.class);
    when(scriptExecutor.executeScript(
            any(), any(), any(), any(), any(), any(), any(), any(), any(), eq(false)))
        .thenThrow(new CancellationException("cancelled"));
    when(mockShellProcessHandler.run(anyList(), any(ShellProcessContext.class)))
        .thenReturn(cancelledResponse());
    RuntimeConfGetter confGetter = mock(RuntimeConfGetter.class);
    when(confGetter.getGlobalConf(GlobalConfKeys.supportBundleEstimateScriptTimeout))
        .thenReturn(Duration.ofMinutes(2));
    SupportBundleScriptEstimateSupport estimateSupport =
        new SupportBundleScriptEstimateSupport(
            app.config(),
            confGetter,
            mockNodeUniverseManager,
            new SupportBundleRunAsSupport(imageBundleUtil, shippedManifestLoader()),
            scriptExecutor,
            mockShellProcessHandler);
    FilesComponentSpec nodeSpec = new FilesComponentSpec();
    nodeSpec.setComponentName("SystemLogs");
    nodeSpec.setScriptPath("bin/support_bundle_utils.sh");
    nodeSpec.setLinuxUser("yugabyte");
    Files.write(devopsHome.resolve("bin").resolve("yba_utils.sh"), new byte[0]);
    FilesComponentSpec ybaSpec = new FilesComponentSpec();
    ybaSpec.setComponentName("YbaHostScript");
    ybaSpec.setScriptPath("bin/yba_utils.sh");
    ybaSpec.setParams(List.of("create_application_logs_bundle"));
    Logger cancellationLogger = (Logger) LoggerFactory.getLogger(SupportBundleCancellation.class);
    ListAppender<ILoggingEvent> cancelled = new ListAppender<>();
    cancelled.start();
    cancellationLogger.addAppender(cancelled);

    assertThrows(
        CancellationException.class,
        () ->
            estimateSupport.runNodeScriptEstimate(
                nodeSpec,
                "SystemLogs",
                new SupportBundleFormData(),
                universe,
                node,
                null,
                null,
                UUID.randomUUID()));
    assertTrue(Thread.interrupted());
    assertThrows(
        CancellationException.class,
        () ->
            estimateSupport.runYbaScriptEstimate(
                ybaSpec, "YbaHostScript", universe, new SupportBundleFormData(), null, null));
    assertTrue(Thread.interrupted());
    cancellationLogger.detachAppender(cancelled);
    assertEquals(
        List.of(
            "Support bundle work cancelled at the estimate of 'SystemLogs' on node 'u-n1'",
            "Support bundle work cancelled at the estimate of 'YbaHostScript' on YBA host"),
        cancelled.list.stream()
            .filter(e -> e.getLevel() == Level.INFO)
            .map(ILoggingEvent::getFormattedMessage)
            .collect(Collectors.toList()));
  }

  /**
   * A node where only yugabyte can reach /home/yugabyte/bin, as on a real one. {@link #nodeCopyMd5}
   * is the checksum of the copy there, null until an upload as yugabyte lands.
   */
  private void stubNodeWhereOnlyYugabyteReachesYbHome() {
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            argThat(cmd -> cmd != null && cmd.size() == 2 && "md5sum".equals(cmd.get(0))),
            any(ShellProcessContext.class),
            eq(false)))
        .thenAnswer(
            invocation -> {
              ShellProcessContext context = invocation.getArgument(3);
              if (!ownsYbHome(context) || nodeCopyMd5 == null) {
                return ShellResponse.create(1, "md5sum: " + REMOTE_PATH + ": Permission denied");
              }
              return runCommandOutput(nodeCopyMd5 + "  " + REMOTE_PATH);
            });
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            argThat((List<String> cmd) -> cmd != null && cmd.contains("mkdir")),
            any(ShellProcessContext.class)))
        .thenAnswer(
            invocation ->
                ownsYbHome(invocation.getArgument(3))
                    ? ShellResponse.create(0, "")
                    : ShellResponse.create(1, "mkdir: cannot create directory: Permission denied"));
    doAnswer(
            invocation -> {
              if (ownsYbHome(invocation.getArgument(5))) {
                uploadedTmpMd5 = localChecksum();
              }
              return null;
            })
        .when(mockNodeUniverseManager)
        .uploadFileToNode(
            eq(node),
            eq(universe),
            any(),
            argThat((String path) -> path != null && path.startsWith(REMOTE_PATH + ".tmp.")),
            any(),
            any());
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            argThat(cmd -> cmd != null && !cmd.isEmpty() && "mv".equals(cmd.get(0))),
            any(ShellProcessContext.class),
            eq(false)))
        .thenAnswer(
            invocation -> {
              if (!ownsYbHome(invocation.getArgument(3)) || uploadedTmpMd5 == null) {
                return ShellResponse.create(1, "mv: Permission denied");
              }
              nodeCopyMd5 = uploadedTmpMd5;
              return ShellResponse.create(0, "");
            });
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            eq(SupportBundleNodeStaging.fileExistsCommand(REMOTE_PATH)),
            any(ShellProcessContext.class),
            eq(false)))
        .thenAnswer(invocation -> fileExistsOutput(nodeCopyMd5 != null));
  }

  private static boolean ownsYbHome(ShellProcessContext context) {
    return ShellProcessContext.DEFAULT_REMOTE_USER.equals(context.getSshUser());
  }

  private String localChecksum() throws Exception {
    return FileUtils.getFileChecksum(localBinary.toString());
  }

  /**
   * Matches the argv exactly, not a prefix of a command string.
   *
   * <p>This used to match a single element starting with "md5sum ", which is what let the checksum
   * never actually run: the call was a shell pipeline in one argv element sent with useBash=false,
   * so no shell ever parsed it and the real output was always empty. The stub handed back a valid
   * checksum regardless, so the tests passed while every node was re-uploaded on every call.
   */
  private void stubRemoteChecksum(String md5) {
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            argThat(cmd -> cmd != null && cmd.size() == 2 && "md5sum".equals(cmd.get(0))),
            any(ShellProcessContext.class),
            eq(false)))
        .thenReturn(runCommandOutput(md5));
  }

  private void stubRename() {
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            argThat(cmd -> cmd != null && !cmd.isEmpty() && "mv".equals(cmd.get(0))),
            any(ShellProcessContext.class),
            eq(false)))
        .thenReturn(ShellResponse.create(0, ""));
  }

  /** Uploaded beside the target, then renamed over it without a shell. */
  private void verifyUploadedThenRenamed(String localPath, String remotePath) {
    ArgumentCaptor<String> tmpPath = ArgumentCaptor.forClass(String.class);
    verify(mockNodeUniverseManager)
        .uploadFileToNode(
            eq(node), eq(universe), eq(localPath), tmpPath.capture(), eq("755"), any());
    assertTrue(tmpPath.getValue(), tmpPath.getValue().startsWith(remotePath + ".tmp."));
    verify(mockNodeUniverseManager)
        .runCommand(
            eq(node),
            eq(universe),
            eq(Arrays.asList("mv", "-f", tmpPath.getValue(), remotePath)),
            any(ShellProcessContext.class),
            eq(false));
  }

  private void stubMkdir() {
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            argThat((List<String> cmd) -> cmd != null && cmd.contains("mkdir")),
            any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, ""));
  }

  private static ShellResponse cancelledResponse() {
    return ShellResponse.create(ShellResponse.ERROR_CODE_EXECUTION_CANCELLED, "cancelled");
  }

  private static ShellResponse runCommandOutput(String output) {
    ShellResponse response = new ShellResponse();
    response.code = 0;
    response.message = ShellResponse.RUN_COMMAND_OUTPUT_PREFIX + "\n" + output;
    return response;
  }

  @Test
  public void testRemoteBinaryPath() {
    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/home/yugabyte");
    assertEquals(
        "/home/yugabyte/bin/yb-dump-cdc-state", cdcStateSupport.remoteBinaryPath(node, universe));
  }

  @Test
  public void testEnsureDumpBinarySkipsUploadWhenPresent() throws Exception {
    // An architecture with no build, so nothing local to verify against. collect_cdc_state trusts
    // the node copy: refusing would fail the component outright rather than protect anything.
    stubNodeArch("sparc64");
    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/home/yugabyte");
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            eq(SupportBundleNodeStaging.fileExistsCommand("/home/yugabyte/bin/yb-dump-cdc-state")),
            any(ShellProcessContext.class),
            eq(false)))
        .thenReturn(fileExistsOutput(true));

    assertTrue(
        cdcStateSupport.ensureDumpBinaryOnNode(
            UUID.randomUUID(),
            node,
            universe,
            ShellProcessContext.builder().description("test").build()));

    verify(mockNodeUniverseManager, never())
        .uploadFileToNode(any(), any(), any(), any(), any(), any());
  }

  @Test
  public void testEnsureDumpBinaryUploadsWhenMissing() throws Exception {
    java.util.Optional<java.nio.file.Path> local =
        cdcStateSupport.resolveLocalBinaryPath(java.util.Optional.of("amd64"));

    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/home/yugabyte");
    // Only the post-upload check: the node's copy is judged by checksum, which is unstubbed here
    // and so reads as unknown.
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            eq(SupportBundleNodeStaging.fileExistsCommand("/home/yugabyte/bin/yb-dump-cdc-state")),
            any(ShellProcessContext.class),
            eq(false)))
        .thenReturn(fileExistsOutput(true));
    stubMkdir();
    stubRename();

    assertTrue(
        cdcStateSupport.ensureDumpBinaryOnNode(
            UUID.randomUUID(),
            node,
            universe,
            ShellProcessContext.builder().description("test").build()));

    verifyUploadedThenRenamed(local.get().toString(), "/home/yugabyte/bin/yb-dump-cdc-state");
  }

  @Test
  public void testCdcStateArchSpecificBinaryName() {
    assertEquals(
        "yb-dump-cdc-state-linux-amd64",
        SupportBundleCdcStateDumpSupport.archSpecificBinaryName("amd64"));
  }

  @Test
  public void testRemoteWorkDirFromTarPath() {
    assertEquals(
        "/tmp/cdc-state-uuid-node-work",
        SupportBundleCdcStateDumpSupport.remoteWorkDirFromTarPath(
            "/tmp/cdc-state-uuid-node.tar.gz"));
  }

  @Test
  public void testAppendRedactionScriptParams() {
    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/home/yugabyte");
    when(mockGFlagsValidation.getSensitiveJsonPathsForVersion(any()))
        .thenReturn(Set.of("$..ycql_ldap_bind_passwd", "$..ysql_hba_conf_csv", "$..foo_flag"));

    List<String> params =
        redactionSupport.appendRedactionScriptParams(
            UUID.randomUUID(),
            Arrays.asList("create_gflags_bundle", "--remote_tar_path", "/tmp/t.tar.gz"),
            universe,
            node);

    assertTrue(params.contains("--yb_software_version"));
    assertTrue(params.contains("--sensitive_gflags_b64"));
    assertTrue(params.contains("--redact_bin"));
    assertTrue(params.contains("/home/yugabyte/bin/yb-redact-server-conf"));

    int b64Index = params.indexOf("--sensitive_gflags_b64") + 1;
    String decoded =
        new String(Base64.getDecoder().decode(params.get(b64Index)), StandardCharsets.UTF_8);
    List<String> gflags = Json.fromJson(Json.parse(decoded), List.class);
    assertTrue(gflags.contains("ycql_ldap_bind_passwd"));
    assertTrue(gflags.contains("foo_flag"));
    assertFalse(gflags.contains("ysql_hba_conf_csv"));
  }

  @Test
  public void testEnsureRedactionBinaryRefusesAnUnverifiedNodeCopy() throws Exception {
    // An architecture with no build, so there is nothing local to verify the node's copy against.
    // Unlike the other two helpers the redactor must refuse: a node running older redaction logic
    // still reports REDACTION_STATUS=NODE, which the collector trusts, so the bundle would look
    // redacted and not be. Returning false makes the caller send --drop_server_conf instead.
    stubNodeArch("sparc64");
    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/home/yugabyte");

    assertFalse(
        redactionSupport.ensureRedactionBinaryOnNode(
            UUID.randomUUID(),
            node,
            universe,
            ShellProcessContext.builder().description("test").build()));

    verify(mockNodeUniverseManager, never())
        .uploadFileToNode(any(), any(), any(), any(), any(), any());
    // Not even probed: what the node has cannot change the answer.
    verify(mockNodeUniverseManager, never())
        .runCommand(
            any(),
            any(),
            argThat(SupportBundleNodeBinarySupportTest::isFileExistsCheck),
            any(),
            anyBoolean());
  }

  /** The arch probe starts a deploy attempt; the upload of the local build finishes it. */
  private void assertDeployAttempted() throws Exception {
    verify(mockNodeUniverseManager)
        .runCommand(eq(node), eq(universe), eq(Arrays.asList("uname", "-m")), any());

    java.util.Optional<java.nio.file.Path> local =
        redactionSupport.resolveLocalBinaryPath(java.util.Optional.of("amd64"));
    verifyUploadedThenRenamed(local.get().toString(), "/home/yugabyte/bin/yb-redact-server-conf");
  }

  @Test
  public void testEnsureRedactionBinaryUploadsWhenMissing() throws Exception {
    lenient()
        .when(mockNodeUniverseManager.getYbHomeDir(node, universe))
        .thenReturn("/home/yugabyte");
    lenient()
        .when(
            mockNodeUniverseManager.runCommand(
                eq(node),
                eq(universe),
                eq(
                    SupportBundleNodeStaging.fileExistsCommand(
                        "/home/yugabyte/bin/yb-redact-server-conf")),
                any(ShellProcessContext.class),
                eq(false)))
        .thenReturn(fileExistsOutput(false), fileExistsOutput(true));
    stubMkdir();
    stubRename();

    redactionSupport.ensureRedactionBinaryOnNode(
        UUID.randomUUID(),
        node,
        universe,
        ShellProcessContext.builder().description("test").build());

    assertDeployAttempted();
  }

  @Test
  public void testEnsureRedactionBinaryStillDeploysWhenExistenceCheckThrows() throws Exception {
    lenient()
        .when(mockNodeUniverseManager.getYbHomeDir(node, universe))
        .thenReturn("/home/yugabyte");
    // An inconclusive check must not be read as "missing and undeployable"; the deploy still runs.
    lenient()
        .when(
            mockNodeUniverseManager.runCommand(
                eq(node),
                eq(universe),
                eq(
                    SupportBundleNodeStaging.fileExistsCommand(
                        "/home/yugabyte/bin/yb-redact-server-conf")),
                any(ShellProcessContext.class),
                eq(false)))
        .thenThrow(new RuntimeException("ssh blew up"))
        .thenReturn(fileExistsOutput(true));
    stubMkdir();
    stubRename();

    redactionSupport.ensureRedactionBinaryOnNode(
        UUID.randomUUID(),
        node,
        universe,
        ShellProcessContext.builder().description("test").build());

    assertDeployAttempted();
  }

  @Test
  public void testAppendDropServerConfScriptParam() {
    List<String> params =
        redactionSupport.appendDropServerConfScriptParam(
            Arrays.asList("create_gflags_bundle", "--remote_tar_path", "/tmp/t.tar.gz"));

    assertEquals(4, params.size());
    assertEquals(SupportBundleServerConfRedactionSupport.DROP_SERVER_CONF_PARAM, params.get(3));
    assertFalse(params.contains("--redact_bin"));
  }

  @Test
  public void testIsKnownRedactionStatus() {
    assertTrue(SupportBundleServerConfRedactionSupport.isKnownRedactionStatus("NODE"));
    assertTrue(SupportBundleServerConfRedactionSupport.isKnownRedactionStatus("FAILED"));
    assertTrue(SupportBundleServerConfRedactionSupport.isKnownRedactionStatus("NOT_APPLICABLE"));
    assertFalse(SupportBundleServerConfRedactionSupport.isKnownRedactionStatus("SKIPPED"));
    assertFalse(SupportBundleServerConfRedactionSupport.isKnownRedactionStatus(null));
    assertFalse(SupportBundleServerConfRedactionSupport.isKnownRedactionStatus("garbage"));
  }

  @Test
  public void testIsServerConfRedactComponent() {
    // The shipped manifest marks GFlags and YbcLogs redactServerConf.
    for (String name : List.of("GFlags", "YbcLogs")) {
      assertTrue(name, redactionSupport.isServerConfRedactComponent(name));
    }
    for (String name :
        List.of(
            "OutputFiles",
            "ErrorFiles",
            "Instance",
            "ConsensusMeta",
            "TabletMeta",
            "NodeAgent",
            "UniverseLogs",
            "SystemLogs",
            "CoreFiles",
            "NoSuchComponent")) {
      assertFalse(name, redactionSupport.isServerConfRedactComponent(name));
    }
  }

  @Test
  public void testIsServerConfRedactComponentFollowsTheManifest() throws Exception {
    // Marking another component in the manifest is all it takes.
    Path home = Files.createTempDirectory("sb-redact-manifest");
    Path manifest = home.resolve("bin/support_bundlev2_manifest.yaml");
    Files.createDirectories(manifest.getParent());
    String shipped =
        new String(
            Files.readAllBytes(Paths.get("devops/bin/support_bundlev2_manifest.yaml")),
            StandardCharsets.UTF_8);
    String marked =
        shipped.replace(
            "    OutputFiles:\n      enabled: true\n",
            "    OutputFiles:\n      enabled: true\n      redactServerConf: true\n");
    assertNotEquals(shipped, marked);
    Files.write(manifest, marked.getBytes(StandardCharsets.UTF_8));
    SupportBundleServerConfRedactionSupport support =
        new SupportBundleServerConfRedactionSupport(
            new SupportBundleNodeBinarySupport(mockNodeUniverseManager),
            mockGFlagsValidation,
            new SupportBundleV2ManifestLoader(
                ConfigFactory.parseString(
                    String.format("yb.devops.home = \"%s\"", home.toAbsolutePath())),
                CustomObjectMapperModule.createDefaultMapper()));

    assertTrue(support.isServerConfRedactComponent("OutputFiles"));
  }

  @Test
  public void testSensitiveGflagsReadOncePerBundle() {
    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/home/yugabyte");
    when(mockGFlagsValidation.getSensitiveJsonPathsForVersion(any()))
        .thenReturn(Set.of("$..ycql_ldap_bind_passwd"));
    UUID bundle = UUID.randomUUID();
    List<String> params = List.of("create_output_files_bundle");

    redactionSupport.appendRedactionScriptParams(bundle, params, universe, node);
    redactionSupport.appendRedactionScriptParams(bundle, params, universe, node);
    verify(mockGFlagsValidation, times(1)).getSensitiveJsonPathsForVersion(any());

    // Another bundle, or this one after it ends, reads it again, so a failed read can't stick.
    redactionSupport.appendRedactionScriptParams(UUID.randomUUID(), params, universe, node);
    redactionSupport.forgetBundle(bundle);
    redactionSupport.appendRedactionScriptParams(bundle, params, universe, node);
    verify(mockGFlagsValidation, times(3)).getSensitiveJsonPathsForVersion(any());
  }

  @Test
  public void testRedactArchSpecificBinaryName() {
    assertEquals(
        "yb-redact-server-conf-linux-amd64",
        SupportBundleServerConfRedactionSupport.archSpecificBinaryName("amd64"));
  }

  private Optional<String> callWith(ShellResponse response) {
    when(mockNodeUniverseManager.runCommand(
            any(), any(), anyList(), any(ShellProcessContext.class), eq(false)))
        .thenReturn(response);
    return SupportBundleRemoteChecksum.remoteMd5(
        mockNodeUniverseManager,
        node,
        universe,
        CHECKSUM_REMOTE_PATH,
        ShellProcessContext.builder().build());
  }

  private static ShellResponse ok(String message) {
    ShellResponse response = new ShellResponse();
    response.code = 0;
    response.message = message;
    return response;
  }

  /** The regression itself: no pipe, no quoting, nothing that needs a shell to exist. */
  @Test
  public void testSendsPlainArgvWithNoShellConstructs() {
    callWith(
        ok(ShellResponse.RUN_COMMAND_OUTPUT_PREFIX + "\n" + MD5 + "  " + CHECKSUM_REMOTE_PATH));

    ArgumentCaptor<List<String>> command = ArgumentCaptor.forClass(List.class);
    verify(mockNodeUniverseManager)
        .runCommand(any(), any(), command.capture(), any(ShellProcessContext.class), eq(false));

    assertEquals(List.of("md5sum", CHECKSUM_REMOTE_PATH), command.getValue());
    String joined = String.join(" ", command.getValue());
    assertFalse(
        "a pipeline needs a shell that useBash=false does not provide", joined.contains("|"));
    assertFalse(joined.contains("2>"));
    assertFalse(joined.contains("'"));
  }

  /** md5sum prints "<checksum> <path>"; only the checksum is wanted. */
  @Test
  public void testParsesTheChecksumFromTheRunCommandForm() {
    Optional<String> md5 =
        callWith(
            ok(ShellResponse.RUN_COMMAND_OUTPUT_PREFIX + "\n" + MD5 + "  " + CHECKSUM_REMOTE_PATH));

    assertTrue(md5.isPresent());
    assertEquals(MD5, md5.get());
  }

  /** Only run_node_action adds the prefix; the node agent does not, and both must work. */
  @Test
  public void testParsesOutputWithoutTheTransportPrefix() {
    Optional<String> md5 = callWith(ok(MD5 + "  " + CHECKSUM_REMOTE_PATH + "\n"));

    assertTrue("a response without the prefix must not be discarded", md5.isPresent());
    assertEquals(MD5, md5.get());
  }

  /** Empty means "unknown", which callers read as "re-upload" -- never as "unchanged". */
  @Test
  public void testUnreadableFileYieldsEmpty() {
    ShellResponse failed = new ShellResponse();
    failed.code = 1;
    failed.message = "md5sum: cannot open";

    assertFalse(callWith(failed).isPresent());
  }

  @Test
  public void testEmptyOutputYieldsEmpty() {
    assertFalse(callWith(ok(ShellResponse.RUN_COMMAND_OUTPUT_PREFIX)).isPresent());
  }

  /** A transport that throws must not take the caller down with it. */
  @Test
  public void testTransportFailureYieldsEmpty() {
    when(mockNodeUniverseManager.runCommand(
            any(), any(), anyList(), any(ShellProcessContext.class), eq(false)))
        .thenThrow(new RuntimeException("node unreachable"));

    assertFalse(
        SupportBundleRemoteChecksum.remoteMd5(
                mockNodeUniverseManager,
                node,
                universe,
                CHECKSUM_REMOTE_PATH,
                ShellProcessContext.builder().build())
            .isPresent());
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

  /** A failed upload's temporary copy sits in the YB home's bin; failing to remove it is a WARN. */
  @Test
  public void testFailedCleanupOfAFailedUploadIsLoggedAtWarn() throws Exception {
    stubRemoteChecksum("");
    stubMkdir();
    doThrow(new RuntimeException("disk full"))
        .when(mockNodeUniverseManager)
        .uploadFileToNode(any(), any(), any(), any(), any(), any());
    when(mockNodeUniverseManager.runCommand(
            eq(node),
            eq(universe),
            argThat(cmd -> cmd != null && cmd.size() == 3 && "rm".equals(cmd.get(0))),
            any(ShellProcessContext.class),
            eq(false)))
        .thenReturn(ShellResponse.create(1, "Permission denied"));

    List<String> warnings =
        logged(
            SupportBundleNodeBinarySupport.class,
            Level.WARN,
            () ->
                assertFalse(
                    binarySupport.ensureBinaryOnNode(
                        null,
                        BINARY,
                        node,
                        universe,
                        ShellProcessContext.builder().build(),
                        "consequence",
                        SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST)));

    assertTrue(
        warnings.toString(),
        warnings.stream()
            .anyMatch(
                w ->
                    w.startsWith("Could not remove '" + REMOTE_PATH + ".tmp.")
                        && w.endsWith("on node u-n1: Permission denied")));
  }
}
