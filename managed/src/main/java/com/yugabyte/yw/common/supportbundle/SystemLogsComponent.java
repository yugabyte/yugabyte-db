package com.yugabyte.yw.common.supportbundle;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.OffsetDateTime;
import java.time.format.DateTimeFormatter;
import java.time.format.DateTimeParseException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.Date;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class SystemLogsComponent implements SupportBundleComponent {
  private final NodeUniverseManager nodeUniverseManager;
  private final RuntimeConfGetter confGetter;

  static final String VAR_LOG_DIR = "/var/log";
  static final String SYSLOGS_ARCHIVE_NAME = "syslogs.tar.gz";
  static final String JOURNAL_ARCHIVE_NAME = "journal.log.gz";

  private static final long JOURNALD_TIMEOUT_SEC = TimeUnit.MINUTES.toSeconds(3);

  /** "Archived and active journals take up 1.5G in the file system." */
  private static final Pattern JOURNAL_DISK_USAGE =
      Pattern.compile("take up\\s+(\\d+(?:\\.\\d+)?)\\s*([KMGTP])?");

  private static final String DISK_USAGE_UNITS = "KMGTP";

  /**
   * Echoed by the access probe, one per run. No token is a substring of another, so the {@code
   * contains} checks in {@link #resolveJournalAccess} cannot mistake one for another.
   */
  private static final String JOURNALCTL_MISSING = "JOURNALCTL_MISSING";

  private static final String SUDO_OK = "SUDO_OK";
  private static final String SUDO_DENIED = "SUDO_DENIED";

  /** short-iso renders 2026-09-03T08:15:00+0000; other builds emit +00:00 or Z. */
  private static final DateTimeFormatter JOURNAL_TIMESTAMP =
      DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH:mm:ss[XXX][XX][X]");

  @Inject
  SystemLogsComponent(NodeUniverseManager nodeUniverseManager, RuntimeConfGetter confGetter) {
    this.nodeUniverseManager = nodeUniverseManager;
    this.confGetter = confGetter;
  }

  public void downloadComponent(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      NodeDetails node)
      throws Exception {
    // pass
  }

  public void downloadComponentBetweenDates(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {
    // The journald supplement is collected independently of the file copy. The journal is volatile
    // on RHEL 7-8 and AL2 (no /var/log/journal), which is exactly where /var/log/messages survives,
    // so neither collection may be skipped because the other one found nothing or failed.
    boolean varLogCollected = false;
    try {
      Map<String, Long> varLogFiles = getVarLogFilesWithSizes(universe, startDate, endDate, node);
      if (!varLogFiles.isEmpty()) {
        downloadVarLogFiles(customer, universe, bundlePath, node, varLogFiles);
        varLogCollected = true;
      }
    } catch (Exception e) {
      // The node listing, downloadRootFiles' sudo probe and its tar all raise on a non-zero exit.
      // A login user without passwordless sudo fails that probe, and that is precisely the node
      // where the journal is the only OS evidence still reachable, so this may not propagate.
      log.error(
          "Error while collecting '{}' files on node {}: ", VAR_LOG_DIR, node.getNodeName(), e);
    }
    boolean journalCollected = downloadJournaldLogs(universe, bundlePath, startDate, endDate, node);

    // The bug this component was fixed for was a silent no-op, so the one state that must never
    // pass unremarked is a node that contributed no OS level evidence at all.
    if (!varLogCollected && !journalCollected) {
      log.warn(
          "Collected no OS system logs for node '{}' of universe '{}': neither a matching file in"
              + " '{}' nor a systemd journal dump. The bundle will contain no OS level evidence for"
              + " this node.",
          node.getNodeName(),
          universe.getName(),
          VAR_LOG_DIR);
    }
  }

  private void downloadVarLogFiles(
      Customer customer,
      Universe universe,
      Path bundlePath,
      NodeDetails node,
      Map<String, Long> res)
      throws Exception {
    Path nodeTargetFile = Paths.get(bundlePath.toString(), SYSLOGS_ARCHIVE_NAME);
    nodeUniverseManager.downloadRootFiles(
        customer.getUuid(), node, universe, res.keySet(), nodeTargetFile.toString());

    // Since we download files from the tmp dir of the db node,
    // after file download the file tree is like
    // yb-support-bundle-asharma-aws-20250207072522.781-logs
    //  '-- yb-admin-asharma-aws-n1
    //     '-- tmp
    //        '-- root_files.tar.gz
    // We fix this below and move the files to the correct dir.
    try {
      if (Files.exists(nodeTargetFile)) {
        File unZippedFile =
            FileUtils.unGzip(
                nodeTargetFile.toAbsolutePath().toFile(), bundlePath.toAbsolutePath().toFile());
        Files.delete(nodeTargetFile);
        FileUtils.unTar(unZippedFile, new File(bundlePath.toAbsolutePath().toString()));
        unZippedFile.delete();
        String tmpDir = nodeUniverseManager.getRemoteTmpDir(node, universe);
        Path rootFileTar = Paths.get(bundlePath.toString(), tmpDir, "root_files.tar.gz");
        if (Files.exists(rootFileTar)) {
          unZippedFile =
              FileUtils.unGzip(
                  rootFileTar.toAbsolutePath().toFile(), bundlePath.toAbsolutePath().toFile());
          FileUtils.unTar(unZippedFile, new File(bundlePath.toAbsolutePath().toString()));
          unZippedFile.delete();
          Path tmpRoot = Paths.get(tmpDir).getName(0);
          FileUtils.deleteDirectory(Paths.get(bundlePath.toString(), tmpRoot.toString()).toFile());
        }
      }
    } catch (Exception e) {
      log.error("Error while collecting system logs: ", e);
    }
  }

  /**
   * Collects the systemd journal for the requested window. This supplements, and never replaces,
   * the /var/log file copy: nodes without persistent journals lose it on reboot, and minimal images
   * (AL2023, some Ubuntu 24.04) have no syslog file at all.
   */
  private boolean downloadJournaldLogs(
      Universe universe, Path bundlePath, Date startDate, Date endDate, NodeDetails node) {
    if (!confGetter.getConfForScope(universe, UniverseConfKeys.collectJournaldLogs)) {
      return false;
    }

    // Resolving the contexts and the tmp dir reads the universe's config and its provider,
    // either of which can throw, and a journald problem must never cost the /var/log copy that
    // already ran. The cleanup below needs the command context, hence the declaration out here.
    String remoteArchive = null;
    ShellProcessContext commandContext = null;
    try {
      ShellProcessContext sshContext = nodeSshContext(node, universe);
      commandContext = journaldContext(sshContext);
      JournalAccess access = resolveJournalAccess(universe, node, commandContext);
      if (access == JournalAccess.UNAVAILABLE) {
        log.debug(
            "journalctl is not available on node {}. Skipping journal collection.",
            node.getNodeName());
        return false;
      }
      if (access == JournalAccess.UNPRIVILEGED) {
        // Left as a warning rather than a skip: on a node whose journal is world readable, or
        // whose login user is in systemd-journal, this still returns everything.
        log.warn(
            "SSH user {} has no passwordless sudo on node {}. The systemd journal will be read"
                + " unprivileged and may be missing most entries.",
            sshContext.getSshUser(),
            node.getNodeName());
      }

      // Unique per run: two bundles collecting this node at once would otherwise overwrite each
      // other's dump, and the cleanup below would delete the archive the other is still copying.
      // The redirect runs as the login user rather than as root, so the archive is owned by that
      // user and needs no sudo to copy back or to clean up.
      remoteArchive =
          String.format(
              "%s/journal-%s.log.gz",
              nodeUniverseManager.getRemoteTmpDir(node, universe), UUID.randomUUID());
      ShellResponse response =
          nodeUniverseManager.runCommand(
              node,
              universe,
              journaldDumpCommand(access, startDate, endDate, remoteArchive),
              commandContext);
      if (!response.isSuccess()) {
        // A missing journalctl, a login user without passwordless sudo and a dump that died
        // halfway all land here, and only the node knows which, so pass its output on.
        log.warn(
            "Could not collect the systemd journal from node {}: {}",
            node.getNodeName(),
            response.getMessage());
        return false;
      }

      Path localArchive = Paths.get(bundlePath.toString(), JOURNAL_ARCHIVE_NAME);
      // Untimed: JOURNALD_TIMEOUT_SEC bounds one journalctl command, not a file transfer.
      nodeUniverseManager.copyFileFromNode(
          node, universe, remoteArchive, localArchive.toString(), sshContext);
      return Files.exists(localArchive);
    } catch (Exception e) {
      log.error("Error while collecting journald logs on node {}: ", node.getNodeName(), e);
      return false;
    } finally {
      if (remoteArchive != null) {
        try {
          nodeUniverseManager.runCommand(
              node, universe, Arrays.asList("rm", "-f", remoteArchive), commandContext);
        } catch (Exception e) {
          log.warn(
              "Could not remove {} from node {}: {}",
              remoteArchive,
              node.getNodeName(),
              e.getMessage());
        }
      }
    }
  }

  /**
   * Dumps the journal for the requested window into {@code archivePath}. pipefail is what makes a
   * half finished dump visible: the pipeline otherwise reports gzip's exit status, and a truncated
   * archive is indistinguishable from a complete one. The path is double quoted because the remote
   * tmp dir is operator configurable and may contain spaces; double quotes survive the transports'
   * own single quoting, unlike the single quotes {@link #bashCommand} forbids.
   */
  private List<String> journaldDumpCommand(
      JournalAccess access, Date startDate, Date endDate, String archivePath) {
    return bashCommand(
        String.format(
            "set -o pipefail; %s --since %s --until %s --no-pager -o short-iso | gzip -c > \"%s\"",
            access.journalctl, epochSeconds(startDate), epochSeconds(endDate), archivePath));
  }

  /**
   * Wraps a shell script in an explicit bash, needed because the ssh transport hands the command to
   * the login user's shell, which is not guaranteed to be bash. Both transports quote only the
   * arguments that contain spaces, so {@code script} must contain no single quotes of its own.
   */
  private List<String> bashCommand(String script) {
    return List.of("bash", "-c", script);
  }

  public Map<String, Long> getFilesListWithSizes(
      Customer customer,
      SupportBundleFormData bundleData,
      Universe universe,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {
    Map<String, Long> res = new HashMap<>();
    try {
      res.putAll(getVarLogFilesWithSizes(universe, startDate, endDate, node));
    } catch (Exception e) {
      // Same reason as in downloadComponentBetweenDates: a listing that raises may not zero out
      // the journal's share of the estimate.
      log.warn(
          "Could not list system log files on node {}: {}", node.getNodeName(), e.getMessage());
    }
    // The journald archive is not a file on the node, so it is reported here for the size estimate
    // only. It must never reach downloadRootFiles, which tars its input paths verbatim.
    long journaldSize = getJournaldSizeEstimate(universe, startDate, endDate, node);
    if (journaldSize > 0L) {
      res.put(JOURNAL_ARCHIVE_NAME, journaldSize);
    }
    return res;
  }

  /**
   * Lists the system log files on the node that cover any part of the requested window. Returns
   * only real paths under /var/log, so the result can be handed to downloadRootFiles as-is.
   */
  private Map<String, Long> getVarLogFilesWithSizes(
      Universe universe, Date startDate, Date endDate, NodeDetails node) {
    Map<String, Long> res = new LinkedHashMap<>();
    Pattern pattern;
    try {
      pattern = compileSystemLogsPattern(universe);
    } catch (RuntimeException e) {
      // The pattern is operator editable, so a bad value has to name itself here rather than
      // surface as a PatternSyntaxException, or as an IndexOutOfBoundsException from group(1).
      log.warn(
          "Cannot select system logs on node '{}': {} is unusable - {}. No file in '{}' will be"
              + " collected. The systemd journal is unaffected.",
          node.getNodeName(),
          UniverseConfKeys.systemLogsRegexPattern.getKey(),
          e.getMessage(),
          VAR_LOG_DIR);
      return res;
    }
    List<VarLogFile> listing = listVarLogFiles(universe, node);

    // Group by base name: each log rotates on its own schedule, so a file's neighbours are only
    // the other rotations of the same log.
    Map<String, List<VarLogFile>> byLog = new LinkedHashMap<>();
    for (VarLogFile file : listing) {
      Matcher matcher = pattern.matcher(file.path);
      if (matcher.matches()) {
        byLog.computeIfAbsent(matcher.group(1), name -> new ArrayList<>()).add(file);
      }
    }

    Date now = new Date();
    for (Entry<String, List<VarLogFile>> entry : byLog.entrySet()) {
      for (VarLogFile file : coveringFiles(entry.getKey(), entry.getValue(), now)) {
        // Keep the file when the period it covers overlaps the requested window at all, so that a
        // file holding only part of the requested period is still collected.
        if (!file.coversFrom.after(endDate) && !file.coversTo.before(startDate)) {
          res.put(file.path, file.size);
        }
      }
    }

    if (res.isEmpty()) {
      // The pattern and the listing size are what tell a reader whether the node has no system
      // logs or whether this distro's naming is simply not covered by the pattern.
      log.warn(
          "No system log file in '{}' on node '{}' of universe '{}' matched '{}' for the window {}"
              + " to {}. {} file(s) were listed. On distros whose rsyslog naming is not covered by"
              + " the pattern this silently yields a bundle with no OS logs.",
          VAR_LOG_DIR,
          node.getNodeName(),
          universe.getName(),
          pattern.pattern(),
          startDate,
          endDate,
          listing.size());
    }
    return res;
  }

  /**
   * Works out the period each of one log's files covers, newest first.
   *
   * <p>logrotate renames rather than rewrites, and both rename and gzip preserve mtime, so a
   * rotated file's mtime is the moment it was rotated: the end of the period it holds. Its start is
   * the mtime of the next older rotation, which is when it became the live file. The oldest
   * rotation has no predecessor, so its start is left open. The live file runs from the newest
   * rotation until now, and on a node that has never rotated this log it is open at both ends.
   *
   * <p>This is the same grouping and descending walk as {@link
   * SupportBundleUtil#filterFilePathsBetweenDates}, with one difference: those logs name themselves
   * after the moment they were created, so that code knows only where each file starts and has to
   * keep one file past the window to catch the one straddling it. Here both edges are known, so the
   * straddling file is selected outright and nothing outside the window is collected. Nothing is
   * inferred from the log's name: a rotation cadence is local configuration that YBA cannot see,
   * and /var/log mixes daily and weekly rotation of dated and numbered files on the same node.
   */
  private List<VarLogFile> coveringFiles(String baseName, List<VarLogFile> files, Date now) {
    VarLogFile live = null;
    List<VarLogFile> rotations = new ArrayList<>();
    for (VarLogFile file : files) {
      // Anything that is not the base name itself is a rotation of it, whatever suffix the
      // distro's logrotate uses.
      if (baseName.equals(Paths.get(file.path).getFileName().toString())) {
        live = file;
      } else {
        rotations.add(file);
      }
    }
    rotations.sort(Comparator.comparing((VarLogFile file) -> file.mtime).reversed());

    List<VarLogFile> ordered = new ArrayList<>();
    if (live != null) {
      live.coversFrom = rotations.isEmpty() ? new Date(Long.MIN_VALUE) : rotations.get(0).mtime;
      live.coversTo = now;
      ordered.add(live);
    }
    for (int i = 0; i < rotations.size(); i++) {
      VarLogFile file = rotations.get(i);
      file.coversFrom =
          i + 1 < rotations.size() ? rotations.get(i + 1).mtime : new Date(Long.MIN_VALUE);
      file.coversTo = file.mtime;
      ordered.add(file);
    }
    return ordered;
  }

  /**
   * Lists /var/log with modification times, in one command. The shared node_utils.sh listing
   * reports only names and sizes, and its three round trips buy scale this does not need: /var/log
   * at depth 1 holds a few dozen files.
   */
  private List<VarLogFile> listVarLogFiles(Universe universe, NodeDetails node) {
    List<VarLogFile> files = new ArrayList<>();
    ShellResponse response =
        nodeUniverseManager.runCommand(
            node,
            universe,
            // Double quotes so bash passes the backslash through to find, which is what expands
            // \n; single quotes are not available here, see bashCommand.
            bashCommand("find " + VAR_LOG_DIR + " -maxdepth 1 -type f -printf \"%T@ %s %p\\n\""),
            ShellProcessContext.DEFAULT);
    if (!response.isSuccess()) {
      log.warn(
          "Could not list '{}' on node {}: {}",
          VAR_LOG_DIR,
          node.getNodeName(),
          response.getMessage());
      return files;
    }
    for (String line : response.extractRunCommandOutput().split("\\R")) {
      // Split on the first two separators only: a path may contain spaces, mtime and size cannot.
      String[] fields = line.trim().split("\\s+", 3);
      if (fields.length < 3) {
        continue;
      }
      try {
        files.add(
            new VarLogFile(
                fields[2],
                Long.parseLong(fields[1]),
                new Date((long) (Double.parseDouble(fields[0]) * 1000L))));
      } catch (NumberFormatException e) {
        log.warn("Ignoring unparseable listing line '{}' from node {}.", line, node.getNodeName());
      }
    }
    return files;
  }

  /** One file in /var/log, with the period it covers once its neighbours are known. */
  private static final class VarLogFile {
    private final String path;
    private final long size;
    private final Date mtime;
    private Date coversFrom;
    private Date coversTo;

    VarLogFile(String path, long size, Date mtime) {
      this.path = path;
      this.size = size;
      this.mtime = mtime;
    }
  }

  /**
   * Prorates the journal's on disk size by the share of its history that the requested window
   * covers. A journal has no cheap exact size for a time window, and measuring one is not an option
   * here: getFilesListWithSizes runs per node inside the synchronous
   * SupportBundleHandler.estimateBundleSize, so reading the window would cost as much as collecting
   * it. The three probes run instead - access, --disk-usage and the oldest entry - read the
   * journal's headers rather than its contents, but they are already more round trips than the one
   * command per component that estimateBundleSize documents, so nothing further belongs here.
   * Reporting zero would under-report the bundle size in the UI, so this is deliberately rough: it
   * assumes a uniform log rate across the journal's history, and treats the binary on disk size as
   * a stand-in for gzipped text, which tends to err high.
   */
  private long getJournaldSizeEstimate(
      Universe universe, Date startDate, Date endDate, NodeDetails node) {
    if (!confGetter.getConfForScope(universe, UniverseConfKeys.collectJournaldLogs)) {
      return 0L;
    }
    try {
      // Silent here by design: the size estimate is best effort, and the download path is where a
      // degraded or missing journal has a consequence worth logging.
      ShellProcessContext context = journaldContext(nodeSshContext(node, universe));
      JournalAccess access = resolveJournalAccess(universe, node, context);
      if (access == JournalAccess.UNAVAILABLE) {
        return 0L;
      }
      long diskUsageBytes = getJournalDiskUsageBytes(universe, node, access, context);
      long windowMillis = endDate.getTime() - startDate.getTime();
      if (diskUsageBytes <= 0L || windowMillis <= 0L) {
        return 0L;
      }
      Date oldestEntry = getOldestJournalEntryDate(universe, node, access, context);
      if (oldestEntry == null) {
        // Unknown history, so report the whole journal rather than under-reporting it.
        return diskUsageBytes;
      }
      long journalSpanMillis = System.currentTimeMillis() - oldestEntry.getTime();
      if (journalSpanMillis <= 0L) {
        return diskUsageBytes;
      }
      return (long) (diskUsageBytes * Math.min(1.0, (double) windowMillis / journalSpanMillis));
    } catch (Exception e) {
      log.warn(
          "Could not estimate journald size on node {}: {}", node.getNodeName(), e.getMessage());
      return 0L;
    }
  }

  private long getJournalDiskUsageBytes(
      Universe universe, NodeDetails node, JournalAccess access, ShellProcessContext context) {
    ShellResponse response =
        nodeUniverseManager.runCommand(
            node, universe, bashCommand(access.journalctl + " --disk-usage --no-pager"), context);
    if (!response.isSuccess()) {
      return 0L;
    }
    Matcher matcher = JOURNAL_DISK_USAGE.matcher(response.extractRunCommandOutput());
    if (!matcher.find()) {
      log.warn("Could not parse 'journalctl --disk-usage' output on node {}.", node.getNodeName());
      return 0L;
    }
    String unit = matcher.group(2);
    int exponent = unit == null ? 0 : DISK_USAGE_UNITS.indexOf(unit) + 1;
    return (long) (Double.parseDouble(matcher.group(1)) * Math.pow(1024, exponent));
  }

  /** The timestamp of the oldest entry the journal still holds, or null if it cannot be read. */
  private Date getOldestJournalEntryDate(
      Universe universe, NodeDetails node, JournalAccess access, ShellProcessContext context) {
    // head closes the pipe after the first line, so this does not read the whole journal. pipefail
    // is deliberately left off: journalctl is then killed by SIGPIPE, and pipefail would surface
    // that as a failed command and discard a line that was in fact received.
    ShellResponse response =
        nodeUniverseManager.runCommand(
            node,
            universe,
            bashCommand(access.journalctl + " --since @0 --no-pager -o short-iso -q | head -1"),
            context);
    if (!response.isSuccess()) {
      return null;
    }
    String firstLine = response.extractRunCommandOutput().trim();
    if (StringUtils.isBlank(firstLine)) {
      return null;
    }
    String timestamp = firstLine.split("\\s+", 2)[0];
    try {
      return Date.from(OffsetDateTime.parse(timestamp, JOURNAL_TIMESTAMP).toInstant());
    } catch (DateTimeParseException e) {
      log.warn(
          "Could not parse the oldest journal entry timestamp '{}' on node {}.",
          timestamp,
          node.getNodeName());
      return null;
    }
  }

  /**
   * Compiles the configured pattern. Group 1 is required rather than optional: the base log name is
   * what groups a log's rotations together, and without it there is nothing to select on.
   */
  private Pattern compileSystemLogsPattern(Universe universe) {
    String regex = confGetter.getConfForScope(universe, UniverseConfKeys.systemLogsRegexPattern);
    Pattern pattern = Pattern.compile(regex);
    if (pattern.matcher("").groupCount() < 1) {
      throw new IllegalArgumentException("'" + regex + "' captures no group for the base log name");
    }
    return pattern;
  }

  /**
   * journalctl parses --since/--until in the node's local timezone, and --utc only changes the
   * output format, so the window is always passed as epoch seconds to avoid silent skew.
   */
  private String epochSeconds(Date date) {
    return "@" + TimeUnit.MILLISECONDS.toSeconds(date.getTime());
  }

  /**
   * Asks the node, in one round trip, whether journalctl exists and whether the login user can sudo
   * without a password. Probing is what makes the unprivileged read possible: putting {@code sudo
   * -n} in the command itself turns a node without passwordless sudo into a node with no journal at
   * all, and the journal is the only OS evidence on images that ship no rsyslog.
   */
  private JournalAccess resolveJournalAccess(
      Universe universe, NodeDetails node, ShellProcessContext context) {
    ShellResponse response =
        nodeUniverseManager.runCommand(
            node,
            universe,
            bashCommand(
                String.format(
                    "if ! command -v journalctl >/dev/null 2>&1; then echo %s;"
                        + " elif sudo -n true >/dev/null 2>&1; then echo %s;"
                        + " else echo %s; fi",
                    JOURNALCTL_MISSING, SUDO_OK, SUDO_DENIED)),
            context);
    if (!response.isSuccess()) {
      log.debug(
          "Could not probe journal access on node {}: {}",
          node.getNodeName(),
          response.getMessage());
      return JournalAccess.UNAVAILABLE;
    }
    String output = response.extractRunCommandOutput();
    if (output.contains(SUDO_OK)) {
      return JournalAccess.PRIVILEGED;
    }
    if (output.contains(SUDO_DENIED)) {
      return JournalAccess.UNPRIVILEGED;
    }
    return JournalAccess.UNAVAILABLE;
  }

  /** How the systemd journal can be read on a node. */
  private enum JournalAccess {
    /** journalctl is not installed, or the node could not be probed: nothing to collect. */
    UNAVAILABLE(null),
    /** Passwordless sudo is available, so the whole journal is readable. */
    PRIVILEGED("sudo -n journalctl"),
    /** No passwordless sudo. Readable, but on most distros only the login user's own entries. */
    UNPRIVILEGED("journalctl");

    private final String journalctl;

    JournalAccess(String journalctl) {
      this.journalctl = journalctl;
    }
  }

  /**
   * The node's ssh context. Util.getProviderForNode reads the provider from the DB on a multicloud
   * cluster, so this is resolved once per entry point and passed down rather than rebuilt for each
   * of the four commands the journald path runs.
   */
  private ShellProcessContext nodeSshContext(NodeDetails node, Universe universe) {
    Provider provider = Util.getProviderForNode(node, universe);
    return ShellProcessContext.builder().sshUser(provider.getDetails().sshUser).build();
  }

  /** {@link #nodeSshContext} bounded by the per-command journald timeout. */
  private ShellProcessContext journaldContext(ShellProcessContext sshContext) {
    return sshContext.toBuilder().timeoutSecs(JOURNALD_TIMEOUT_SEC).build();
  }
}
