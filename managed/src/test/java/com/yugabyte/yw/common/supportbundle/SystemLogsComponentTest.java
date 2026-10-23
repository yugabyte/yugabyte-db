// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.atLeastOnce;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.ArgumentMatchers;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import org.slf4j.LoggerFactory;

@RunWith(MockitoJUnitRunner.class)
public class SystemLogsComponentTest extends FakeDBApplication {
  @Mock public NodeUniverseManager mockNodeUniverseManager;
  @Mock public RuntimeConfGetter mockConfGetter;

  /** Kept in sync with yb.support_bundle.system_logs_regex_pattern in reference.conf. */
  private static final String SYSTEM_LOGS_REGEX =
      "(?:.*/)?(messages|syslog)(?:[.-]\\d{1,8})?(?:\\.gz)?";

  private static final String VAR_LOG = "/var/log/";
  private static final String VAR_LOG_DIR_NAME = "/var/log";
  private static final String REMOTE_TMP_DIR = "/tmp";
  private static final String DEFAULT_DISK_USAGE =
      "Archived and active journals take up 128.0M in the file system.";
  private static final String DEFAULT_OLDEST_ENTRY =
      "2025-06-01T10:11:12+0000 u-n1 systemd: Started something.";
  private static final String SUDO_OK = "SUDO_OK";
  private static final String SUDO_DENIED = "SUDO_DENIED";
  private static final String JOURNALCTL_MISSING = "JOURNALCTL_MISSING";
  private static final long ONE_MIB = 1024L * 1024L;
  private static final long FILE_SIZE = 10L;

  private Universe universe;
  private Customer customer;
  private ListAppender<ILoggingEvent> logEvents;
  private final String fakeSupportBundleBasePath = "/tmp/yugaware_tests/support_bundle-system_logs";
  private final Path fakeBundlePath =
      Paths.get(fakeSupportBundleBasePath, "yb-support-bundle-test-20220308000000.000-logs");
  private final NodeDetails node = new NodeDetails();
  private SystemLogsComponent systemLogsComponent;

  @Before
  public void setUp() throws Exception {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());

    node.nodeName = "u-n1";
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "fake_ip";
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            (u) -> {
              UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
              node.placementUuid = universeDetails.getPrimaryCluster().uuid;
              universeDetails.nodeDetailsSet = new HashSet<>(Arrays.asList(node));
              u.setUniverseDetails(universeDetails);
            });

    Files.createDirectories(fakeBundlePath);

    // A support bundle has no per component error channel, so the warning is the only signal that
    // a node contributed nothing. Capture it rather than trusting it stays in place.
    logEvents = new ListAppender<>();
    logEvents.start();
    ((Logger) LoggerFactory.getLogger(SystemLogsComponent.class)).addAppender(logEvents);

    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.systemLogsRegexPattern)))
        .thenReturn(SYSTEM_LOGS_REGEX);
    // One answer for every node command: the component issues the /var/log listing and the
    // journald commands through the same runCommand, so they cannot be stubbed separately.
    when(mockNodeUniverseManager.getRemoteTmpDir(any(), any())).thenReturn(REMOTE_TMP_DIR);
    when(mockNodeUniverseManager.runCommand(
            any(), any(), ArgumentMatchers.<List<String>>any(), any()))
        .thenAnswer(
            invocation -> {
              List<String> parts = invocation.getArgument(2);
              // Re-stubbing invokes the mock to record matchers, and that call carries no args.
              return parts == null ? null : answerNodeCommand(String.join(" ", parts));
            });

    systemLogsComponent = new SystemLogsComponent(mockNodeUniverseManager, mockConfGetter);
  }

  @After
  public void tearDown() throws Exception {
    ((Logger) LoggerFactory.getLogger(SystemLogsComponent.class)).detachAppender(logEvents);
    logEvents.stop();
    FileUtils.deleteDirectory(Paths.get(fakeSupportBundleBasePath).toFile());
  }

  // ---------------------------------------------------------------------------------------------
  // Helpers
  // ---------------------------------------------------------------------------------------------

  private void disableJournald() {
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.collectJournaldLogs)))
        .thenReturn(false);
  }

  private void enableJournald() {
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.collectJournaldLogs)))
        .thenReturn(true);
  }

  /** Asserts that one captured WARN carries every one of the given fragments. */
  private void assertWarned(String... fragments) {
    for (ILoggingEvent event : logEvents.list) {
      if (!"WARN".equals(event.getLevel().toString())) {
        continue;
      }
      String rendered = event.getFormattedMessage();
      boolean all = true;
      for (String fragment : fragments) {
        all = all && rendered.contains(fragment);
      }
      if (all) {
        return;
      }
    }
    throw new AssertionError(
        "No WARN contained " + Arrays.toString(fragments) + ". Captured: " + renderedWarnings());
  }

  private void assertNotWarned(String fragment) {
    for (ILoggingEvent event : logEvents.list) {
      if ("WARN".equals(event.getLevel().toString())
          && event.getFormattedMessage().contains(fragment)) {
        throw new AssertionError("Unexpected WARN containing '" + fragment + "'");
      }
    }
  }

  private List<String> renderedWarnings() {
    List<String> out = new ArrayList<>();
    for (ILoggingEvent event : logEvents.list) {
      if ("WARN".equals(event.getLevel().toString())) {
        out.add(event.getFormattedMessage());
      }
    }
    return out;
  }

  private static ShellResponse successResponse(String output) {
    return ShellResponse.create(
        ShellResponse.ERROR_CODE_SUCCESS, ShellResponse.RUN_COMMAND_OUTPUT_PREFIX + output);
  }

  private static ShellResponse failureResponse(String output) {
    return ShellResponse.create(ShellResponse.ERROR_CODE_GENERIC_ERROR, output);
  }

  /** Lines the node's find returns, newest listed first for readability. */
  private final List<String> varLogListing = new ArrayList<>();

  private boolean varLogListingFails = false;
  private ShellResponse journaldDumpResponse = successResponse("");
  private String diskUsageOutput = DEFAULT_DISK_USAGE;
  private String oldestEntryLine = DEFAULT_OLDEST_ENTRY;
  private String accessProbeOutput = SUDO_OK;

  /**
   * Answers every command the component runs against the node: the /var/log listing, the journal
   * access probe, the --disk-usage and oldest-entry probes, and the windowed dump.
   */
  private ShellResponse answerNodeCommand(String command) {
    if (command.contains("find " + VAR_LOG_DIR_NAME)) {
      return varLogListingFails
          ? failureResponse("find: '/var/log': Permission denied")
          : successResponse(String.join("\n", varLogListing));
    }
    // The probe script mentions journalctl, so it has to be matched before the dump.
    if (command.contains("command -v journalctl")) {
      return successResponse(accessProbeOutput);
    }
    if (command.contains("--disk-usage")) {
      return diskUsageOutput == null
          ? failureResponse("journalctl: command not found")
          : successResponse(diskUsageOutput);
    }
    if (command.contains("head -1")) {
      return oldestEntryLine == null ? failureResponse("") : successResponse(oldestEntryLine);
    }
    if (command.contains("journalctl")) {
      return journaldDumpResponse;
    }
    return successResponse("");
  }

  /**
   * Adds one /var/log file with the given modification time. mtime is the whole input to file
   * selection now, so every test states it rather than relying on a name convention.
   */
  private void varLogFile(String fileName, Date mtime) {
    varLogListing.add(
        String.format(
            "%d.0000000000 %d %s%s",
            TimeUnit.MILLISECONDS.toSeconds(mtime.getTime()), FILE_SIZE, VAR_LOG, fileName));
  }

  /** Files that are all being written right now: enough for tests that do not exercise windows. */
  private void mockVarLog(String... fileNames) {
    for (String fileName : fileNames) {
      varLogFile(fileName, new Date());
    }
  }

  private void mockJournaldCommand(ShellResponse dumpResponse) {
    journaldDumpResponse = dumpResponse;
  }

  private void mockJournaldCommands(
      ShellResponse dumpResponse, String diskUsage, String oldestEntry) {
    mockJournaldCommands(dumpResponse, diskUsage, oldestEntry, SUDO_OK);
  }

  /** A null disk usage or oldest entry makes that probe fail. */
  private void mockJournaldCommands(
      ShellResponse dumpResponse, String diskUsage, String oldestEntry, String accessProbe) {
    this.journaldDumpResponse = dumpResponse;
    this.diskUsageOutput = diskUsage;
    this.oldestEntryLine = oldestEntry;
    this.accessProbeOutput = accessProbe;
  }

  private static boolean isVarLogListing(List<String> command) {
    return command != null && command.stream().anyMatch(part -> part.contains("find /var/log"));
  }

  private static boolean isJournaldCommand(List<String> command) {
    return command.stream().anyMatch(part -> part.contains("journalctl"));
  }

  private static boolean isJournaldDumpCommand(List<String> command) {
    return command.stream().anyMatch(part -> part.contains("gzip -c >"));
  }

  private long journaldEstimate(String diskUsageOutput, String oldestEntryLine, int windowDays)
      throws Exception {
    return journaldEstimate(diskUsageOutput, oldestEntryLine, windowDays, SUDO_OK);
  }

  private long journaldEstimate(
      String diskUsageOutput, String oldestEntryLine, int windowDays, String accessProbeOutput)
      throws Exception {
    enableJournald();
    mockVarLog();
    mockJournaldCommands(successResponse(""), diskUsageOutput, oldestEntryLine, accessProbeOutput);
    return systemLogsComponent
        .getFilesListWithSizes(customer, null, universe, daysAgo(windowDays), new Date(), node)
        .getOrDefault(SystemLogsComponent.JOURNAL_ARCHIVE_NAME, 0L);
  }

  /** The journalctl pipeline, as a single bash script argument. */
  private String capturedJournaldScript() {
    @SuppressWarnings("unchecked")
    ArgumentCaptor<List<String>> commandCaptor = ArgumentCaptor.forClass(List.class);
    verify(mockNodeUniverseManager, atLeastOnce())
        .runCommand(any(), any(), commandCaptor.capture(), any());
    return commandCaptor.getAllValues().stream()
        .filter(SystemLogsComponentTest::isJournaldDumpCommand)
        .map(command -> command.get(command.size() - 1))
        .reduce((first, second) -> first)
        .orElseThrow(() -> new AssertionError("No journalctl command was issued"));
  }

  /** Stands in for the file copy off the node, which is what puts the journal in the bundle. */
  private void mockJournalArrivesInBundle() {
    doAnswer(
            invocation -> {
              Files.createFile(Paths.get(invocation.getArgument(3).toString()));
              return null;
            })
        .when(mockNodeUniverseManager)
        .copyFileFromNode(any(), any(), anyString(), anyString(), any());
  }

  private Date daysAgo(int days) {
    return new Date(System.currentTimeMillis() - TimeUnit.DAYS.toMillis(days));
  }

  private Date parseDate(String yyyyMMdd) throws Exception {
    return new SimpleDateFormat("yyyyMMdd").parse(yyyyMMdd);
  }

  private Set<String> selectedFiles(Date startDate, Date endDate) throws Exception {
    return systemLogsComponent
        .getFilesListWithSizes(customer, null, universe, startDate, endDate, node)
        .keySet();
  }

  private static Set<String> varLogPaths(String... fileNames) {
    Set<String> paths = new HashSet<>();
    for (String fileName : fileNames) {
      paths.add(VAR_LOG + fileName);
    }
    return paths;
  }

  // ---------------------------------------------------------------------------------------------
  // File selection
  // ---------------------------------------------------------------------------------------------

  /** Regression test for PLAT-22181: Ubuntu nodes used to silently yield nothing. */
  @Test
  public void testUbuntuSystemLogsAreCollected() throws Exception {
    disableJournald();
    varLogFile("syslog", new Date());
    varLogFile("syslog.1", daysAgo(1));
    varLogFile("syslog.2.gz", daysAgo(2));

    Set<String> selected = selectedFiles(daysAgo(3), new Date());

    assertEquals(varLogPaths("syslog", "syslog.1", "syslog.2.gz"), selected);
  }

  /**
   * The failure this component was fixed for, and the one the ordinal heuristic still had: RHEL
   * rotates messages weekly, so for a window nine days back the file that holds it is messages.2 -
   * which any "one rotation per day" placement puts a week too recent and drops.
   */
  @Test
  public void testWeeklyRotationIsPlacedByItsOwnMtime() throws Exception {
    disableJournald();
    varLogFile("messages", new Date());
    varLogFile("messages.1", daysAgo(1));
    varLogFile("messages.2", daysAgo(8));
    varLogFile("messages.3", daysAgo(15));

    // messages.2 was rotated 8 days ago and so holds days 15 to 8; nothing else reaches the window.
    assertEquals(varLogPaths("messages.2"), selectedFiles(daysAgo(10), daysAgo(9)));
  }

  /** Two logs rotating on different schedules, with no cadence configured anywhere. */
  @Test
  public void testDailyAndWeeklyRotationOnTheSameNode() throws Exception {
    disableJournald();
    varLogFile("syslog", new Date());
    varLogFile("syslog.1", daysAgo(1));
    varLogFile("syslog.2", daysAgo(2));
    varLogFile("syslog.3", daysAgo(3));
    varLogFile("messages", new Date());
    varLogFile("messages.1", daysAgo(7));
    varLogFile("messages.2", daysAgo(14));

    // syslog.2 covers days 3 to 2 and syslog.1 days 2 to 1; messages has not rotated since day 7,
    // so its live file is the one holding day 2.
    assertEquals(
        varLogPaths("syslog.1", "syslog.2", "messages"), selectedFiles(daysAgo(3), daysAgo(2)));
  }

  /** A rotation covers from the previous rotation up to its own mtime, and no further. */
  @Test
  public void testRotationCoversUpToItsOwnMtime() throws Exception {
    disableJournald();
    varLogFile("messages", parseDate("20260210"));
    varLogFile("messages-20260201", parseDate("20260201"));
    varLogFile("messages-20260108", parseDate("20260108"));
    varLogFile("messages-20260101", parseDate("20260101"));

    // messages-20260108 holds Jan 1 to Jan 8, which is the only file overlapping Jan 3 to Jan 6.
    assertEquals(
        varLogPaths("messages-20260108"),
        selectedFiles(parseDate("20260103"), parseDate("20260106")));
  }

  /** The oldest rotation has no predecessor, so it is open ended below. */
  @Test
  public void testOldestRotationIsOpenEndedBelow() throws Exception {
    disableJournald();
    varLogFile("messages", new Date());
    varLogFile("messages-20260108", parseDate("20260108"));

    assertTrue(
        selectedFiles(parseDate("20200101"), parseDate("20200105"))
            .contains(VAR_LOG + "messages-20260108"));
  }

  /**
   * The live file runs from the newest rotation until now, so a window that ends before that
   * rotation is not in it. This matches ApplicationLogsComponent, which collects application.log
   * only when the window reaches the current day.
   */
  @Test
  public void testLiveFileIsSkippedForAWindowEntirelyBeforeTheLastRotation() throws Exception {
    disableJournald();
    varLogFile("syslog", new Date());
    varLogFile("syslog.1", daysAgo(2));

    Set<String> selected = selectedFiles(daysAgo(5), daysAgo(3));

    assertFalse(selected.contains(VAR_LOG + "syslog"));
    assertEquals(varLogPaths("syslog.1"), selected);
  }

  @Test
  public void testLiveFileIsCollectedWhenTheWindowReachesNow() throws Exception {
    disableJournald();
    varLogFile("syslog", new Date());
    varLogFile("syslog.1", daysAgo(2));

    assertTrue(selectedFiles(daysAgo(1), new Date()).contains(VAR_LOG + "syslog"));
  }

  /** A log that has never rotated is open at both ends, so it answers for any window. */
  @Test
  public void testNeverRotatedLogIsAlwaysCollected() throws Exception {
    disableJournald();
    varLogFile("syslog", new Date());

    assertEquals(
        varLogPaths("syslog"), selectedFiles(parseDate("20200101"), parseDate("20200105")));
  }

  @Test
  public void testNumericRotationVariants() throws Exception {
    disableJournald();
    varLogFile("messages", new Date());
    varLogFile("messages.1.gz", daysAgo(1));
    varLogFile("syslog", new Date());
    varLogFile("syslog-1.gz", daysAgo(1));

    Set<String> selected = selectedFiles(daysAgo(2), new Date());

    // Both the dot and hyphen rotation separators are recognised, with or without .gz.
    assertEquals(varLogPaths("messages", "messages.1.gz", "syslog", "syslog-1.gz"), selected);
  }

  @Test
  public void testUnrelatedVarLogFilesAreIgnored() throws Exception {
    disableJournald();
    mockVarLog("btmp", "wtmp", "lastlog", "dpkg.log", "syslog");

    assertEquals(varLogPaths("syslog"), selectedFiles(daysAgo(1), new Date()));
  }

  /**
   * PLAT-22181 asks for the rsyslog catch-all logs and the journal, not for the credential and
   * kernel logs beside them. They are one alternation edit away for an operator who wants them.
   */
  @Test
  public void testCredentialAndKernelLogsAreNotCollectedByDefault() throws Exception {
    disableJournald();
    mockVarLog("auth.log", "auth.log.1", "secure", "secure-20260108", "kern.log", "syslog");

    assertEquals(varLogPaths("syslog"), selectedFiles(daysAgo(1), new Date()));
  }

  /** A log name added to the regex needs nothing else: its rotations date themselves. */
  @Test
  public void testAddedLogNameNeedsNoRotationConfig() throws Exception {
    disableJournald();
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.systemLogsRegexPattern)))
        .thenReturn("(?:.*/)?(daemon\\.log)(?:[.-][^/]*)?");
    varLogFile("daemon.log", parseDate("20260210"));
    varLogFile("daemon.log-20260108", parseDate("20260108"));
    varLogFile("daemon.log-20251201", parseDate("20251201"));
    varLogFile("syslog", new Date());

    Set<String> selected = selectedFiles(parseDate("20260103"), parseDate("20260106"));

    // daemon.log-20260108 holds Dec 1 to Jan 8 and overlaps the window; syslog is not in the regex.
    assertEquals(varLogPaths("daemon.log-20260108"), selected);
  }

  /** A listing that fails collects no file rather than a wrong set of them. */
  @Test
  public void testFailedListingCollectsNoFile() throws Exception {
    disableJournald();
    varLogListingFails = true;

    assertTrue(selectedFiles(daysAgo(1), new Date()).isEmpty());

    assertWarned("Could not list", VAR_LOG_DIR_NAME, node.getNodeName());
  }

  // ---------------------------------------------------------------------------------------------
  // Download behaviour
  // ---------------------------------------------------------------------------------------------

  @Test
  public void testDownloadPassesOnlyRealVarLogPaths() throws Exception {
    enableJournald();
    mockVarLog("syslog", "syslog.1");
    mockJournaldCommand(successResponse(""));
    mockJournalArrivesInBundle();

    systemLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, fakeBundlePath, daysAgo(1), new Date(), node);

    @SuppressWarnings("unchecked")
    ArgumentCaptor<Set<String>> pathsCaptor = ArgumentCaptor.forClass(Set.class);
    verify(mockNodeUniverseManager, times(1))
        .downloadRootFiles(any(), any(), any(), pathsCaptor.capture(), anyString());
    // The synthetic journald estimate entry must never reach the sudo tar path set.
    assertEquals(varLogPaths("syslog", "syslog.1"), pathsCaptor.getValue());
    assertNotWarned("no OS level evidence");
  }

  /** An empty listing must not reach the sudo tar, which fails on an empty path set. */
  @Test
  public void testEmptyVarLogSkipsDownload() throws Exception {
    disableJournald();
    mockVarLog();

    systemLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, fakeBundlePath, daysAgo(1), new Date(), node);

    verify(mockNodeUniverseManager, never())
        .downloadRootFiles(any(), any(), any(), any(), anyString());
  }

  /**
   * downloadRootFiles raises on a non-zero exit from its sudo probe or its tar, and a login user
   * without passwordless sudo is exactly what fails that probe. That is also the node where the
   * journal is the only OS evidence left to get, so the file copy failing may not take it down.
   */
  @Test
  public void testFailedVarLogCopyKeepsJournald() throws Exception {
    enableJournald();
    mockVarLog("syslog");
    mockJournaldCommands(
        successResponse(""), DEFAULT_DISK_USAGE, DEFAULT_OLDEST_ENTRY, SUDO_DENIED);
    mockJournalArrivesInBundle();
    doThrow(new RuntimeException("Error occurred. Code: 1. Output: sudo: a password is required"))
        .when(mockNodeUniverseManager)
        .downloadRootFiles(any(), any(), any(), any(), anyString());

    systemLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, fakeBundlePath, daysAgo(1), new Date(), node);

    assertTrue(Files.exists(fakeBundlePath.resolve(SystemLogsComponent.JOURNAL_ARCHIVE_NAME)));
    // The journal reached the bundle, so this node did contribute OS evidence.
    assertNotWarned("no OS level evidence");
  }

  /** A /var/log listing the node refuses must not cost the journal either. */
  @Test
  public void testFailedVarLogListingKeepsJournald() throws Exception {
    enableJournald();
    varLogListingFails = true;
    mockJournaldCommand(successResponse(""));
    mockJournalArrivesInBundle();

    systemLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, fakeBundlePath, daysAgo(1), new Date(), node);

    verify(mockNodeUniverseManager, never())
        .downloadRootFiles(any(), any(), any(), any(), anyString());
    assertTrue(Files.exists(fakeBundlePath.resolve(SystemLogsComponent.JOURNAL_ARCHIVE_NAME)));
  }

  /**
   * The regression this component was fixed for was a silent no-op, so a node that contributed
   * neither a log file nor a journal must say so, and must say what it looked for.
   */
  @Test
  public void testNodeWithNoOsEvidenceAtAllIsReported() throws Exception {
    disableJournald();
    mockVarLog("btmp", "wtmp", "dpkg.log");

    systemLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, fakeBundlePath, daysAgo(1), new Date(), node);

    assertWarned("No system log file in", node.getNodeName(), SYSTEM_LOGS_REGEX, "3 file(s)");
    assertWarned("no OS system logs", node.getNodeName(), "no OS level evidence");
  }

  /** A journal dump alone is OS evidence, so the combined warning must stay quiet. */
  @Test
  public void testJournalAloneIsNotReportedAsNoEvidence() throws Exception {
    enableJournald();
    mockVarLog("dpkg.log");
    mockJournaldCommand(successResponse(""));
    mockJournalArrivesInBundle();

    systemLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, fakeBundlePath, daysAgo(1), new Date(), node);

    assertWarned("No system log file in", node.getNodeName());
    assertNotWarned("no OS level evidence");
  }

  /** A failed journal dump on a node that also has no matching file is the worst case. */
  @Test
  public void testFailedJournalAndNoFilesIsReported() throws Exception {
    enableJournald();
    mockVarLog();
    mockJournaldCommand(failureResponse("bash: journalctl: command not found"));

    systemLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, fakeBundlePath, daysAgo(1), new Date(), node);

    assertWarned("Could not collect the systemd journal", node.getNodeName());
    assertWarned("no OS level evidence", node.getNodeName());
  }

  // ---------------------------------------------------------------------------------------------
  // journald supplement
  // ---------------------------------------------------------------------------------------------

  @Test
  public void testJournaldIsCollectedAlongsideFiles() throws Exception {
    enableJournald();
    mockVarLog("syslog");
    mockJournaldCommand(successResponse(""));
    mockJournalArrivesInBundle();

    Date startDate = daysAgo(1);
    Date endDate = new Date();
    systemLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, fakeBundlePath, startDate, endDate, node);

    // The file copy still happened: journald supplements it, never replaces it.
    verify(mockNodeUniverseManager, times(1))
        .downloadRootFiles(any(), any(), any(), any(), anyString());

    String script = capturedJournaldScript();
    // Epoch seconds, since journalctl parses --since/--until in the node's local timezone.
    assertTrue(script.contains("--since @" + TimeUnit.MILLISECONDS.toSeconds(startDate.getTime())));
    assertTrue(script.contains("--until @" + TimeUnit.MILLISECONDS.toSeconds(endDate.getTime())));
    // Without pipefail the pipeline reports gzip's exit status and a truncated dump looks clean.
    assertTrue(script.startsWith("set -o pipefail;"));
    assertTrue(script.contains("sudo -n journalctl --since"));
    // A unique remote name per run, so concurrent bundles for this node cannot clobber each
    // other, and quoted so an operator configured tmp dir with a space still redirects correctly.
    assertTrue(
        script,
        script.matches(
            ".*\\| gzip -c > \"" + REMOTE_TMP_DIR + "/journal-[0-9a-f-]{36}\\.log\\.gz\"$"));

    assertTrue(Files.exists(fakeBundlePath.resolve(SystemLogsComponent.JOURNAL_ARCHIVE_NAME)));
    // The archive is removed from the node whether or not the collection succeeded.
    verify(mockNodeUniverseManager, times(1))
        .runCommand(
            any(),
            any(),
            ArgumentMatchers.<List<String>>argThat(command -> command.contains("rm")),
            any());
  }

  /** The pattern is operator editable, so a bad value must name itself and cost only the files. */
  @Test
  public void testUnusablePatternIsReportedAndDoesNotCostTheJournal() throws Exception {
    enableJournald();
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.systemLogsRegexPattern)))
        .thenReturn("messages(");
    mockJournaldCommand(successResponse(""));
    mockJournalArrivesInBundle();

    systemLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, fakeBundlePath, daysAgo(1), new Date(), node);

    assertWarned(
        "yb.support_bundle.system_logs_regex_pattern", node.getNodeName(), "No file in '/var/log'");
    // Unusable means unusable: the node is not even listed.
    verify(mockNodeUniverseManager, never())
        .runCommand(
            any(),
            any(),
            ArgumentMatchers.<List<String>>argThat(SystemLogsComponentTest::isVarLogListing),
            any());
    assertTrue(Files.exists(fakeBundlePath.resolve(SystemLogsComponent.JOURNAL_ARCHIVE_NAME)));
  }

  /**
   * Without group 1 there is no base name to group a log's rotations by, so the pattern is dead
   * even though it compiles.
   */
  @Test
  public void testPatternWithoutACaptureGroupIsReported() throws Exception {
    disableJournald();
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.systemLogsRegexPattern)))
        .thenReturn("(?:.*/)?messages");

    assertTrue(selectedFiles(daysAgo(1), new Date()).isEmpty());

    assertWarned("yb.support_bundle.system_logs_regex_pattern", "captures no group");
  }

  /**
   * A node without journalctl, a login user without passwordless sudo and a dump that died halfway
   * are all a failed command, and none of them may cost the /var/log copy.
   */
  @Test
  public void testFailedJournaldCollectionKeepsFiles() throws Exception {
    enableJournald();
    mockVarLog("messages");
    mockJournaldCommand(failureResponse("bash: journalctl: command not found"));

    systemLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, fakeBundlePath, daysAgo(1), new Date(), node);

    // Nothing is copied off the node, and the /var/log copy is untouched.
    verify(mockNodeUniverseManager, never())
        .copyFileFromNode(any(), any(), anyString(), anyString(), any());
    verify(mockNodeUniverseManager, times(1))
        .downloadRootFiles(any(), any(), any(), any(), anyString());
  }

  /**
   * A login user without passwordless sudo must still get whatever the journal will give up: on a
   * minimal image the journal is the only OS evidence there is.
   */
  @Test
  public void testJournaldIsReadUnprivilegedWithoutSudo() throws Exception {
    enableJournald();
    mockVarLog("syslog");
    mockJournaldCommands(
        successResponse(""), DEFAULT_DISK_USAGE, DEFAULT_OLDEST_ENTRY, SUDO_DENIED);
    mockJournalArrivesInBundle();

    systemLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, fakeBundlePath, daysAgo(1), new Date(), node);

    String script = capturedJournaldScript();
    assertFalse("the dump must not use sudo when sudo was denied", script.contains("sudo"));
    assertTrue(script.contains("journalctl --since"));
    // The journal still reaches the bundle, and the degradation is on the record.
    assertTrue(Files.exists(fakeBundlePath.resolve(SystemLogsComponent.JOURNAL_ARCHIVE_NAME)));
    assertWarned("no passwordless sudo", node.getNodeName(), "may be missing most entries");
  }

  /** A node without journalctl has no journal to collect, and must not be asked for one. */
  @Test
  public void testJournaldSkippedWhenJournalctlIsMissing() throws Exception {
    enableJournald();
    mockVarLog();
    mockJournaldCommands(
        successResponse(""), DEFAULT_DISK_USAGE, DEFAULT_OLDEST_ENTRY, JOURNALCTL_MISSING);

    systemLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, fakeBundlePath, daysAgo(1), new Date(), node);

    verify(mockNodeUniverseManager, never())
        .runCommand(
            any(),
            any(),
            ArgumentMatchers.<List<String>>argThat(SystemLogsComponentTest::isJournaldDumpCommand),
            any());
    verify(mockNodeUniverseManager, never())
        .copyFileFromNode(any(), any(), anyString(), anyString(), any());
    // No file and no journal is the state that must never pass unremarked.
    assertWarned("no OS level evidence", node.getNodeName());
  }

  /** The estimate degrades the same way the collection does. */
  @Test
  public void testJournaldEstimateWithoutSudo() throws Exception {
    long unprivileged = journaldEstimate(DEFAULT_DISK_USAGE, DEFAULT_OLDEST_ENTRY, 30, SUDO_DENIED);
    assertTrue("an unprivileged journal still has a size", unprivileged > 0L);

    @SuppressWarnings("unchecked")
    ArgumentCaptor<List<String>> commandCaptor = ArgumentCaptor.forClass(List.class);
    verify(mockNodeUniverseManager, atLeastOnce())
        .runCommand(any(), any(), commandCaptor.capture(), any());
    for (List<String> command : commandCaptor.getAllValues()) {
      String joined = String.join(" ", command);
      if (joined.contains("--disk-usage") || joined.contains("head -1")) {
        assertFalse("probes must not use sudo when sudo was denied", joined.contains("sudo"));
      }
    }
  }

  @Test
  public void testJournaldEstimateIsZeroWhenJournalctlIsMissing() throws Exception {
    assertEquals(
        0L, journaldEstimate(DEFAULT_DISK_USAGE, DEFAULT_OLDEST_ENTRY, 30, JOURNALCTL_MISSING));
  }

  @Test
  public void testJournaldSizeIsIncludedInTheEstimate() throws Exception {
    enableJournald();
    mockVarLog("syslog");
    mockJournaldCommand(successResponse(""));

    Map<String, Long> sizes =
        systemLogsComponent.getFilesListWithSizes(
            customer, null, universe, daysAgo(1), new Date(), node);

    // The journal is reported alongside the real /var/log paths, never in place of them.
    assertTrue(sizes.containsKey(SystemLogsComponent.JOURNAL_ARCHIVE_NAME));
    assertTrue(sizes.containsKey(VAR_LOG + "syslog"));
    long journaldSize = sizes.get(SystemLogsComponent.JOURNAL_ARCHIVE_NAME);
    assertTrue("expected a non zero estimate, got " + journaldSize, journaldSize > 0L);
  }

  /**
   * The estimate is the journal's on disk size prorated by the share of its history the window
   * covers, so a one day window over a journal reaching back years is a small fraction of it.
   */
  @Test
  public void testJournaldEstimateIsProratedByWindowShare() throws Exception {
    long wholeJournal = 128L * ONE_MIB;
    long oneDay = journaldEstimate(DEFAULT_DISK_USAGE, DEFAULT_OLDEST_ENTRY, 1);
    long thirtyDays = journaldEstimate(DEFAULT_DISK_USAGE, DEFAULT_OLDEST_ENTRY, 30);

    assertTrue("one day should be a fraction of the journal", oneDay < wholeJournal / 10);
    assertTrue("a longer window should estimate more", thirtyDays > oneDay);
    assertTrue("the estimate is capped at the whole journal", thirtyDays <= wholeJournal);
  }

  /** A window longer than the journal's history cannot exceed the journal itself. */
  @Test
  public void testJournaldEstimateIsCappedAtTheWholeJournal() throws Exception {
    String oldest = "2026-09-01T00:00:00+0000 u-n1 systemd: Started something.";
    assertEquals(128L * ONE_MIB, journaldEstimate(DEFAULT_DISK_USAGE, oldest, 3650));
  }

  /** Unknown history must report the whole journal rather than under-report the bundle. */
  @Test
  public void testJournaldEstimateFallsBackToWholeJournalWithoutHistory() throws Exception {
    assertEquals(128L * ONE_MIB, journaldEstimate(DEFAULT_DISK_USAGE, null, 1));
  }

  /** Every size unit journalctl prints has to resolve, or the estimate is silently wrong. */
  @Test
  public void testJournaldDiskUsageUnitsAreParsed() throws Exception {
    assertEquals(
        8L * 1024L, journaldEstimate("journals take up 8.0K in the file system.", null, 1));
    assertEquals(
        (long) (1.5 * 1024 * ONE_MIB),
        journaldEstimate("journals take up 1.5G in the file system.", null, 1));
    assertEquals(512L, journaldEstimate("journals take up 512 in the file system.", null, 1));
  }

  /** An unreadable or unparseable journal reports nothing rather than a fabricated number. */
  @Test
  public void testJournaldEstimateIsZeroWhenTheJournalCannotBeSized() throws Exception {
    assertEquals(0L, journaldEstimate(null, DEFAULT_OLDEST_ENTRY, 1));
    assertEquals(0L, journaldEstimate("no size in this output", DEFAULT_OLDEST_ENTRY, 1));
  }

  /** The estimate must stay cheap: it may not read the window's bytes off the node. */
  @Test
  public void testJournaldEstimateDoesNotReadTheWindow() throws Exception {
    journaldEstimate(DEFAULT_DISK_USAGE, DEFAULT_OLDEST_ENTRY, 1);

    @SuppressWarnings("unchecked")
    ArgumentCaptor<List<String>> commandCaptor = ArgumentCaptor.forClass(List.class);
    verify(mockNodeUniverseManager, atLeastOnce())
        .runCommand(any(), any(), commandCaptor.capture(), any());
    for (List<String> command : commandCaptor.getAllValues()) {
      String joined = String.join(" ", command);
      assertFalse("estimate must not count the window's bytes", joined.contains("wc -c"));
      assertFalse("estimate must not dump the window", joined.contains("gzip -c >"));
      assertFalse("estimate must not scan a bounded window", joined.contains("--until"));
    }
  }

  /** A listing the node refuses must not zero out the journal's share of the estimate. */
  @Test
  public void testEstimateSurvivesFailedVarLogListing() throws Exception {
    enableJournald();
    varLogListingFails = true;
    mockJournaldCommand(successResponse(""));

    Map<String, Long> sizes =
        systemLogsComponent.getFilesListWithSizes(
            customer, null, universe, daysAgo(1), new Date(), node);

    assertEquals(Set.of(SystemLogsComponent.JOURNAL_ARCHIVE_NAME), sizes.keySet());
  }

  @Test
  public void testJournaldDisabledLeavesEstimateToFilesOnly() throws Exception {
    disableJournald();
    mockVarLog("syslog");

    Map<String, Long> sizes =
        systemLogsComponent.getFilesListWithSizes(
            customer, null, universe, daysAgo(1), new Date(), node);

    assertEquals(varLogPaths("syslog"), sizes.keySet());
    verify(mockNodeUniverseManager, never())
        .runCommand(
            any(),
            any(),
            ArgumentMatchers.<List<String>>argThat(SystemLogsComponentTest::isJournaldCommand),
            any());
  }
}
