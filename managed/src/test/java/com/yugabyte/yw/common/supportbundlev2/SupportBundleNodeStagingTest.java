// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import ch.qos.logback.classic.Level;
import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import org.apache.commons.io.FileUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import org.slf4j.LoggerFactory;

@RunWith(MockitoJUnitRunner.class)
public class SupportBundleNodeStagingTest {

  private static final Path LOCAL = Paths.get("/opt/yugaware/devops/bin/support_bundle_utils.sh");
  private static final ShellProcessContext CONTEXT = ShellProcessContext.builder().build();

  @Mock private NodeUniverseManager mockNodeUniverseManager;
  @Mock private Universe universe;

  private SupportBundleNodeStaging staging;
  private final UUID id = UUID.randomUUID();
  private final NodeDetails node1 = node("n1", "10.0.0.1");
  private final NodeDetails node2 = node("n2", "10.0.0.2");

  @Before
  public void setUp() {
    staging = new SupportBundleNodeStaging(mockNodeUniverseManager);
  }

  private static NodeDetails node(String name, String ip) {
    NodeDetails node = new NodeDetails();
    node.nodeName = name;
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = ip;
    return node;
  }

  private static ShellResponse response(int code) {
    ShellResponse response = new ShellResponse();
    response.code = code;
    response.message = code == 0 ? "" : "Permission denied";
    return response;
  }

  private void mkdirSucceeds() {
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any(), eq(false)))
        .thenReturn(response(0));
  }

  private static String dir(UUID stagingId, String ip) {
    return "/tmp/yba-support-bundle-" + stagingId + "-" + ip.replace('.', '_');
  }

  @Test
  public void testStageUploadsOnceAndReturnsTheCopysPath() {
    mkdirSucceeds();

    String first = staging.stage(id, universe, node1, LOCAL, CONTEXT);
    String second = staging.stage(id, universe, node1, LOCAL, CONTEXT);

    String expected = dir(id, "10.0.0.1") + "/support_bundle_utils.sh";
    assertEquals(expected, first);
    assertEquals(expected, second);
    verify(mockNodeUniverseManager, times(1))
        .runCommand(
            eq(node1),
            eq(universe),
            eq(List.of("mkdir", "-p", "-m", "755", dir(id, "10.0.0.1"))),
            eq(CONTEXT),
            eq(false));
    verify(mockNodeUniverseManager, times(1))
        .uploadFileToNode(node1, universe, LOCAL.toString(), expected, "755", CONTEXT);
    assertTrue(staging.isStaged(id, node1, "support_bundle_utils.sh"));
    assertFalse(staging.isStaged(id, node1, "other.sh"));
  }

  @Test
  public void testEachNodeAndStagingIdGetsItsOwnDirectory() {
    mkdirSucceeds();
    UUID other = UUID.randomUUID();

    assertEquals(
        dir(id, "10.0.0.2") + "/support_bundle_utils.sh",
        staging.stage(id, universe, node2, LOCAL, CONTEXT));
    assertEquals(
        dir(other, "10.0.0.1") + "/support_bundle_utils.sh",
        staging.stage(other, universe, node1, LOCAL, CONTEXT));
    assertFalse(staging.isStaged(id, node1, "support_bundle_utils.sh"));
  }

  @Test
  public void testFailedMkdirThrowsAndIsRetriedNextTime() {
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any(), eq(false)))
        .thenReturn(response(1), response(0));

    RuntimeException thrown =
        assertThrows(
            RuntimeException.class, () -> staging.stage(id, universe, node1, LOCAL, CONTEXT));
    assertTrue(thrown.getMessage(), thrown.getMessage().contains("Permission denied"));
    assertFalse(staging.isStaged(id, node1, "support_bundle_utils.sh"));

    staging.stage(id, universe, node1, LOCAL, CONTEXT);
    assertTrue(staging.isStaged(id, node1, "support_bundle_utils.sh"));
  }

  @Test
  public void testFailedUploadThrows() {
    mkdirSucceeds();
    doThrow(new RuntimeException("No space left on device"))
        .when(mockNodeUniverseManager)
        .uploadFileToNode(any(), any(), anyString(), anyString(), anyString(), any());

    assertThrows(RuntimeException.class, () -> staging.stage(id, universe, node1, LOCAL, CONTEXT));
    assertFalse(staging.isStaged(id, node1, "support_bundle_utils.sh"));
  }

  @Test
  public void testCleanupRemovesOnlyThisIdsDirectories() {
    mkdirSucceeds();
    UUID other = UUID.randomUUID();
    staging.stage(id, universe, node1, LOCAL, CONTEXT);
    staging.stage(id, universe, node2, LOCAL, CONTEXT);
    staging.stage(other, universe, node1, LOCAL, CONTEXT);

    List<NodeDetails> cleaned = staging.cleanup(id, universe);

    assertEquals(2, cleaned.size());
    verify(mockNodeUniverseManager)
        .runCommand(
            eq(node1),
            eq(universe),
            eq(List.of("rm", "-rf", dir(id, "10.0.0.1"))),
            any(),
            eq(false));
    verify(mockNodeUniverseManager)
        .runCommand(
            eq(node2),
            eq(universe),
            eq(List.of("rm", "-rf", dir(id, "10.0.0.2"))),
            any(),
            eq(false));
    verify(mockNodeUniverseManager, never())
        .runCommand(
            any(), any(), eq(List.of("rm", "-rf", dir(other, "10.0.0.1"))), any(), eq(false));
    assertFalse(staging.isStaged(id, node1, "support_bundle_utils.sh"));
    assertTrue(staging.isStaged(other, node1, "support_bundle_utils.sh"));
  }

  @Test
  public void testCleanupNeverThrows() {
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any(), eq(false)))
        .thenReturn(response(0))
        .thenThrow(new RuntimeException("node unreachable"));
    staging.stage(id, universe, node1, LOCAL, CONTEXT);

    assertEquals(1, staging.cleanup(id, universe).size());
    assertFalse(staging.isStaged(id, node1, "support_bundle_utils.sh"));
  }

  @Test
  public void testCleanupOfANullIdIsANoOp() {
    assertTrue(staging.cleanup(null, universe).isEmpty());
    verify(mockNodeUniverseManager, never()).runCommand(any(), any(), anyList(), any(), eq(false));
  }

  private static ShellResponse checkPrints(String answer) {
    ShellResponse response = response(0);
    response.message = ShellResponse.RUN_COMMAND_OUTPUT_PREFIX + "\n" + answer + "\n";
    return response;
  }

  @Test
  public void testFileExistsRunsTheCheckWithThePathAsAnArgument() {
    String path = "/tmp/a $(touch x).tar.gz";
    when(mockNodeUniverseManager.runCommand(
            eq(node1),
            eq(universe),
            eq(List.of("bash", "-c", SupportBundleNodeStaging.FILE_EXISTS_SCRIPT, "bash", path)),
            eq(CONTEXT),
            eq(false)))
        .thenReturn(checkPrints("1"));

    assertTrue(
        SupportBundleNodeStaging.fileExists(
            mockNodeUniverseManager, node1, universe, path, CONTEXT));
    verify(mockNodeUniverseManager, never()).runScript(any(), any(), any(), anyList(), any());
  }

  @Test
  public void testFileExistsCommandAnswersOneOrZeroUnderBash() throws Exception {
    Path dir = Files.createTempDirectory("sb-exists");
    try {
      Path present = Files.createFile(dir.resolve("a $(touch pwned).tar.gz"));
      assertEquals("1", runIn(dir, SupportBundleNodeStaging.fileExistsCommand(present.toString())));
      assertEquals(
          "0", runIn(dir, SupportBundleNodeStaging.fileExistsCommand(dir + "/missing.tar.gz")));
      assertFalse(Files.exists(dir.resolve("pwned")));
    } finally {
      FileUtils.deleteDirectory(dir.toFile());
    }
  }

  private static String runIn(Path dir, List<String> command) throws Exception {
    Process process =
        new ProcessBuilder(command).directory(dir.toFile()).redirectErrorStream(true).start();
    String output = new String(process.getInputStream().readAllBytes()).trim();
    assertEquals(0, process.waitFor());
    return output;
  }

  @Test
  public void testFileExistsIsFalseWhenTheNodePrintsZero() {
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any(), eq(false)))
        .thenReturn(checkPrints("0"));

    assertFalse(
        SupportBundleNodeStaging.fileExists(
            mockNodeUniverseManager, node1, universe, "/tmp/t.tar.gz", CONTEXT));
  }

  @Test
  public void testFileExistsThrowsWhenTheCheckFails() {
    // Output that reads as "exists" must not hide the failure.
    ShellResponse failed = checkPrints("1");
    failed.code = 1;
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any(), eq(false)))
        .thenReturn(failed);

    assertThrows(
        RuntimeException.class,
        () ->
            SupportBundleNodeStaging.fileExists(
                mockNodeUniverseManager, node1, universe, "/tmp/t.tar.gz", CONTEXT));
  }

  /** A cancelled command is an abort, not a failure the caller logs and moves past. */
  @Test
  public void testCancelledCommandsThrowCancellation() {
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any(), eq(false)))
        .thenReturn(response(ShellResponse.ERROR_CODE_EXECUTION_CANCELLED));

    assertThrows(
        CancellationException.class,
        () ->
            SupportBundleNodeStaging.fileExists(
                mockNodeUniverseManager, node1, universe, "/tmp/t.tar.gz", CONTEXT));
    assertThrows(
        CancellationException.class, () -> staging.stage(id, universe, node1, LOCAL, CONTEXT));
    verify(mockNodeUniverseManager, never())
        .uploadFileToNode(any(), any(), anyString(), anyString(), anyString(), any());
  }

  /** A thread still running after its bundle's cleanup can stage nothing, so it runs nothing. */
  @Test
  public void testAClosedIdStagesNothing() {
    staging.close(id);

    assertThrows(
        CancellationException.class, () -> staging.stage(id, universe, node1, LOCAL, CONTEXT));
    verify(mockNodeUniverseManager, never()).runCommand(any(), any(), anyList(), any(), eq(false));

    mkdirSucceeds();
    staging.stage(UUID.randomUUID(), universe, node1, LOCAL, CONTEXT);
  }

  /** Cleanup waits for a directory still being created, so it removes it rather than missing it. */
  @Test
  public void testCleanupWaitsForADirectoryBeingCreated() throws Exception {
    CountDownLatch inMkdir = new CountDownLatch(1);
    CountDownLatch release = new CountDownLatch(1);
    List<String> mkdir = List.of("mkdir", "-p", "-m", "755", dir(id, "10.0.0.1"));
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any(), eq(false)))
        .thenAnswer(
            invocation -> {
              if (mkdir.equals(invocation.getArgument(2))) {
                inMkdir.countDown();
                release.await();
              }
              return response(0);
            });
    AtomicReference<Throwable> thrown = new AtomicReference<>();
    Thread stager = start(() -> staging.dir(id, universe, node1, CONTEXT), thrown);
    assertTrue(inMkdir.await(10, TimeUnit.SECONDS));

    staging.close(id);
    Thread cleaner = start(() -> staging.cleanup(id, universe), thrown);
    awaitBlockedOrTerminated(cleaner);
    release.countDown();
    stager.join(10_000);
    cleaner.join(10_000);

    assertNull(thrown.get());
    verify(mockNodeUniverseManager)
        .runCommand(
            eq(node1),
            eq(universe),
            eq(List.of("rm", "-rf", dir(id, "10.0.0.1"))),
            any(),
            eq(false));
  }

  /**
   * A thread that passed the closed check before the bundle ended creates nothing once it gets the
   * node's lock; here it waits on another thread's failed mkdir while the id is closed.
   */
  @Test
  public void testALateThreadCreatesNoDirectoryOnceTheIdIsClosed() throws Exception {
    CountDownLatch inMkdir = new CountDownLatch(1);
    CountDownLatch release = new CountDownLatch(1);
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any(), eq(false)))
        .thenAnswer(
            invocation -> {
              inMkdir.countDown();
              release.await();
              return response(1);
            })
        .thenReturn(response(0));
    Thread first = start(() -> staging.dir(id, universe, node1, CONTEXT), new AtomicReference<>());
    assertTrue(inMkdir.await(10, TimeUnit.SECONDS));
    AtomicReference<Throwable> lateThrown = new AtomicReference<>();
    Thread late = start(() -> staging.dir(id, universe, node1, CONTEXT), lateThrown);
    awaitBlockedOrTerminated(late);
    assertEquals(Thread.State.BLOCKED, late.getState());

    staging.close(id);
    release.countDown();
    first.join(10_000);
    late.join(10_000);

    assertTrue(String.valueOf(lateThrown.get()), lateThrown.get() instanceof CancellationException);
    verify(mockNodeUniverseManager, times(1)).runCommand(any(), any(), anyList(), any(), eq(false));
    assertTrue(staging.cleanup(id, universe).isEmpty());
  }

  private static Thread start(Runnable body, AtomicReference<Throwable> thrown) {
    Thread thread =
        new Thread(
            () -> {
              try {
                body.run();
              } catch (Throwable t) {
                thrown.set(t);
              }
            });
    thread.start();
    return thread;
  }

  private static void awaitBlockedOrTerminated(Thread thread) throws InterruptedException {
    long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10);
    while (thread.getState() != Thread.State.BLOCKED
        && thread.getState() != Thread.State.TERMINATED
        && System.nanoTime() < deadline) {
      Thread.sleep(10);
    }
  }

  /** Under the node's tmp directory, so an operator who moved it off a small /tmp is followed. */
  @Test
  public void testDirectoryFollowsTheNodesTmpDirectory() {
    when(mockNodeUniverseManager.getRemoteTmpDir(node1, universe)).thenReturn("/data/tmp");
    mkdirSucceeds();
    String expected = "/data/tmp/yba-support-bundle-" + id + "-10_0_0_1";

    assertEquals(expected, staging.dir(id, universe, node1, CONTEXT));
    assertEquals(
        expected + "/support_bundle_utils.sh", staging.stage(id, universe, node1, LOCAL, CONTEXT));
    staging.cleanup(id, universe);

    verify(mockNodeUniverseManager, times(1))
        .runCommand(
            any(), any(), eq(List.of("mkdir", "-p", "-m", "755", expected)), any(), eq(false));
    verify(mockNodeUniverseManager)
        .runCommand(any(), any(), eq(List.of("rm", "-rf", expected)), any(), eq(false));
  }

  /** A tmp directory that can't be used falls back to /tmp, and cleanup removes the one used. */
  @Test
  public void testUnusableTmpDirectoryFallsBackToTmp() {
    when(mockNodeUniverseManager.getRemoteTmpDir(node1, universe)).thenReturn("/data/tmp");
    when(mockNodeUniverseManager.getRemoteTmpDir(node2, universe))
        .thenThrow(new RuntimeException("no provider"));
    String refused = "/data/tmp/yba-support-bundle-" + id + "-10_0_0_1";
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any(), eq(false)))
        .thenAnswer(
            invocation ->
                response(invocation.getArgument(2, List.class).contains(refused) ? 1 : 0));

    assertEquals(dir(id, "10.0.0.1"), staging.dir(id, universe, node1, CONTEXT));
    assertEquals(dir(id, "10.0.0.2"), staging.dir(id, universe, node2, CONTEXT));
    staging.cleanup(id, universe);

    verify(mockNodeUniverseManager)
        .runCommand(any(), any(), eq(List.of("rm", "-rf", dir(id, "10.0.0.1"))), any(), eq(false));
    verify(mockNodeUniverseManager, never())
        .runCommand(any(), any(), eq(List.of("rm", "-rf", refused)), any(), eq(false));
  }

  @Test
  public void testNodeKeyIsOnePathSegment() {
    assertEquals("fd00__1", SupportBundleNodeStaging.nodeKey(node("n", "fd00::1")));
    NodeDetails noIp = new NodeDetails();
    noIp.nodeName = "u-n3";
    assertEquals("u-n3", SupportBundleNodeStaging.nodeKey(noIp));
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

  /** One INFO line per node whose directory was removed, and none for a removal that failed. */
  @Test
  public void testCleanupLogsEachRemovedDirectory() throws Exception {
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any(), eq(false)))
        .thenAnswer(
            invocation -> {
              List<String> command = invocation.getArgument(2);
              boolean failed = command.get(0).equals("rm") && command.get(2).endsWith("10_0_0_2");
              return response(failed ? 1 : 0);
            });
    staging.stage(id, universe, node1, LOCAL, CONTEXT);
    staging.stage(id, universe, node2, LOCAL, CONTEXT);

    List<String> infos =
        logged(SupportBundleNodeStaging.class, Level.INFO, () -> staging.cleanup(id, universe));

    assertEquals(
        List.of("Removed support bundle directory " + dir(id, "10.0.0.1") + " on node 'n1'"),
        infos);
  }
}
