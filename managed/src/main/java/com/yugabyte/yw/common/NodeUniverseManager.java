// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.NodeAgentPoller;
import com.yugabyte.yw.common.concurrent.KeyLock;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.time.DateUtils;
import play.libs.Json;

/**
 * Runs commands, scripts and file transfers on DB nodes, over node agent gRPC, Kubernetes or SSH.
 *
 * <p>Only SSH re-parses argv (ssh.py joins it into one line), so with {@code useBash=false} {@link
 * #runCommand} compensates through {@link #escapeForSshTransport}, and {@link #runScript} quotes
 * its params on every transport. {@code NodeUniverseManagerTransportTest} pins this per transport;
 * extend it for callers with unusual quoting.
 */
@Slf4j
@Singleton
public class NodeUniverseManager extends DevopsBase {
  public static final ShellProcessContext DEFAULT_CONTEXT =
      ShellProcessContext.builder().logCmdOutput(true).build();
  // Characters no shell treats specially; Python's shlex.quote leaves the same set bare.
  private static final Pattern SHELL_SAFE_ARG = Pattern.compile("[A-Za-z0-9_@%+=:,./-]+");
  public static final String NODE_ACTION_SSH_SCRIPT = "bin/run_node_action.py";
  public static final String CERTS_DIR = "/yugabyte-tls-config";
  public static final String K8S_CERTS_DIR = "/opt/certs/yugabyte";
  public static final String NODE_UTILS_SCRIPT = "bin/node_utils.sh";

  /** Forces support bundle ysqlsh sessions read-only, independent of the statement allow list. */
  static final String SUPPORT_BUNDLE_READ_ONLY_PGOPTIONS =
      "export PGOPTIONS='-c default_transaction_read_only=on';";

  private final KeyLock<UUID> universeLock = new KeyLock<>();

  @Inject ImageBundleUtil imageBundleUtil;
  @Inject NodeAgentPoller nodeAgentPoller;
  @Inject LocalNodeUniverseManager localNodeUniverseManager;
  @Inject NodeActionRunner nodeActionRunner;

  @Override
  protected String getCommandType() {
    return "node";
  }

  public void downloadNodeLogs(NodeUniverseContext context, String targetLocalFile) {
    UUID universeUUID = context.getUniverse().getUniverseUUID();
    universeLock.acquireLock(universeUUID);
    try {
      Optional<NodeAgent> optional =
          maybeUpgradeAndGetNodeAgent(context.getUniverse(), context.getNode());
      if (optional.isPresent()) {
        nodeActionRunner.downloadLogs(
            optional.get(),
            context.getNode(),
            getYbHomeDir(context.getNode(), context.getUniverse()),
            targetLocalFile,
            context.getShellProcessContext());
      } else {
        List<String> actionArgs = new ArrayList<>();
        actionArgs.add("--yb_home_dir");
        actionArgs.add(getYbHomeDir(context.getNode(), context.getUniverse()));
        actionArgs.add("--target_local_file");
        actionArgs.add(targetLocalFile);
        executeNodeAction(UniverseNodeAction.DOWNLOAD_LOGS, context, actionArgs).processErrors();
      }
    } finally {
      universeLock.releaseLock(universeUUID);
    }
  }

  public String getLocalTmpDir() {
    String localTmpDir = confGetter.getGlobalConf(GlobalConfKeys.ybTmpDirectoryPath);
    if (localTmpDir == null || localTmpDir.isEmpty()) {
      localTmpDir = "/tmp";
    }
    return localTmpDir;
  }

  public String getRemoteTmpDir(NodeDetails node, Universe universe) {
    String remoteTmpDir = GFlagsUtil.getCustomTmpDirectory(confGetter, node, universe);
    if (remoteTmpDir == null || remoteTmpDir.isEmpty()) {
      remoteTmpDir = "/tmp";
    }
    return remoteTmpDir;
  }

  public String createTempFileWithSourceFiles(List<String> sourceNodeFiles) {
    String tempFilePath =
        getLocalTmpDir() + "/" + UUID.randomUUID().toString() + "-source-files.txt";
    try {
      Files.createFile(Paths.get(tempFilePath));
    } catch (IOException e) {
      log.error("Error creating file: {}", e.getMessage());
      return null;
    }

    try (PrintWriter out = new PrintWriter(new FileWriter(tempFilePath))) {
      for (String value : sourceNodeFiles) {
        out.println(value);
        log.info(value);
      }
      log.info("Above values written to file.");
    } catch (IOException e) {
      log.error("Error writing to file: {}", e.getMessage());
      return null;
    }
    return tempFilePath;
  }

  public void downloadNodeFile(
      NodeDetails node,
      Universe universe,
      String ybHomeDir,
      List<String> sourceNodeFiles,
      String targetLocalFile) {
    String filesListFilePath = "", remoteFilesListPath = "";
    try {
      filesListFilePath = createTempFileWithSourceFiles(sourceNodeFiles);
      if (filesListFilePath == null) {
        throw new RuntimeException(
            "Could not create temp file while downloading node file for universe "
                + universe.getUniverseUUID());
      }
      remoteFilesListPath =
          getRemoteTmpDir(node, universe)
              + "/"
              + Paths.get(filesListFilePath).getFileName().toString();
      Optional<NodeAgent> optional = maybeUpgradeAndGetNodeAgent(universe, node);
      NodeUniverseContext context = NodeUniverseContext.of(universe, node);
      if (optional.isPresent()) {
        nodeActionRunner.downloadFile(
            optional.get(),
            node,
            ybHomeDir,
            filesListFilePath,
            remoteFilesListPath,
            targetLocalFile,
            context.getShellProcessContext());
      } else {
        List<String> actionArgs = new ArrayList<>();
        // yb_home_dir denotes a custom starting directory for the remote file. (Eg: ~/, /mnt/d0,
        // etc.)
        actionArgs.add("--yb_home_dir");
        actionArgs.add(ybHomeDir);
        actionArgs.add("--source_node_files_path");
        actionArgs.add(filesListFilePath);
        actionArgs.add("--remote_node_files_path");
        actionArgs.add(remoteFilesListPath);
        actionArgs.add("--target_local_file");
        actionArgs.add(targetLocalFile);
        executeNodeAction(UniverseNodeAction.DOWNLOAD_FILE, context, actionArgs).processErrors();
      }
    } finally {
      FileUtils.deleteQuietly(new File(filesListFilePath));
    }
  }

  public void copyFileFromNode(
      NodeDetails node, Universe universe, String remoteFile, String localFile) {
    copyFileFromNode(NodeUniverseContext.of(universe, node), remoteFile, localFile);
  }

  /**
   * Download the given set of root files in a compress archive called root_files.tar.gz to the
   * target local path.
   *
   * @param paths Absolute paths to the root files.
   */
  public void downloadRootFiles(
      UUID customerUUID,
      NodeDetails node,
      Universe universe,
      Set<String> paths,
      String targetLocalPath) {
    String nodeTmpDir = getRemoteTmpDir(node, universe);
    Provider provider = Util.getProviderForNode(node, universe);
    NodeUniverseContext context =
        NodeUniverseContext.of(
            universe,
            node,
            ShellProcessContext.builder().sshUser(provider.getDetails().sshUser).build());

    // Check if we have sudo access.
    List<String> cmds = new ArrayList<>(Arrays.asList("sudo", "-v"));
    if (!runCommand(context, cmds).processErrors().isSuccess()) {
      log.warn(
          "SSH user {} doesn't have sudo access on node {}. Unable to download root files.",
          provider.getDetails().sshUser,
          node.getNodeName());
      return;
    }

    log.info(
        "Downloading the following root files: {} from node {}.",
        paths.toString(),
        node.getNodeName());

    // Collect all files in a compressed archive in the tmp dir.
    // --warning=no-file-changed: live logs (e.g. /var/log/messages) can change during read; GNU tar
    // otherwise exits 1 and the support bundle skips system logs.
    cmds = new ArrayList<>(Arrays.asList("sudo", "tar", "--warning=no-file-changed", "-czhf"));
    String pathToTar = nodeTmpDir + "/root_files.tar.gz";
    cmds.add(pathToTar);
    cmds.addAll(paths);
    runCommand(context, cmds).processErrors();

    downloadNodeFile(node, universe, "/", List.of(pathToTar), targetLocalPath);

    // Delete tar from tmp dir on node.
    cmds = new ArrayList<>(Arrays.asList("sudo", "rm"));
    cmds.add(pathToTar);
    runCommand(context, cmds).processErrors();
  }

  public void copyFileFromNode(NodeUniverseContext context, String remoteFile, String localFile) {
    Universe universe = context.getUniverse();
    NodeDetails node = context.getNode();
    Optional<NodeAgent> optional =
        context.getShellProcessContext().isUseSshConnectionOnly()
            ? Optional.empty()
            : maybeUpgradeAndGetNodeAgent(universe, node);
    if (optional.isPresent()) {
      nodeActionRunner.copyFile(
          optional.get(), remoteFile, localFile, context.getShellProcessContext());
    } else {
      List<String> actionArgs = new ArrayList<>();
      actionArgs.add("--remote_file");
      actionArgs.add(remoteFile);
      actionArgs.add("--local_file");
      actionArgs.add(localFile);
      executeNodeAction(UniverseNodeAction.COPY_FILE, context, actionArgs).processErrors();
    }
  }

  public ShellResponse bulkCheckFilesExist(
      NodeDetails node,
      Universe universe,
      String ybDir,
      String sourceFilesPath,
      String targetLocalFilePath) {
    universeLock.acquireLock(universe.getUniverseUUID());
    try {
      List<String> actionArgs = new ArrayList<>();
      actionArgs.add("--yb_dir");
      actionArgs.add(ybDir);
      actionArgs.add("--source_files_to_check_path");
      actionArgs.add(sourceFilesPath);
      actionArgs.add("--target_local_file_path");
      actionArgs.add(targetLocalFilePath);
      return executeNodeAction(
          UniverseNodeAction.BULK_CHECK_FILES_EXIST,
          NodeUniverseContext.of(universe, node),
          actionArgs);
    } finally {
      universeLock.releaseLock(universe.getUniverseUUID());
    }
  }

  public void uploadFileToNode(
      NodeUniverseContext context, String sourceFile, String targetFile, String permissions) {
    Universe universe = context.getUniverse();
    NodeDetails node = context.getNode();
    ShellProcessContext shellProcessContext = context.getShellProcessContext();
    Optional<NodeAgent> optional =
        shellProcessContext.isUseSshConnectionOnly()
            ? Optional.empty()
            : maybeUpgradeAndGetNodeAgent(universe, node);
    if (optional.isPresent()) {
      nodeActionRunner.uploadFile(
          optional.get(), sourceFile, targetFile, permissions, shellProcessContext);
    } else {
      List<String> actionArgs = new ArrayList<>();
      actionArgs.add("--source_file");
      actionArgs.add(sourceFile);
      actionArgs.add("--target_file");
      actionArgs.add(targetFile);
      actionArgs.add("--permissions");
      actionArgs.add(permissions);
      executeNodeAction(UniverseNodeAction.UPLOAD_FILE, context, actionArgs).processErrors();
    }
  }

  public ShellResponse runCommand(NodeUniverseContext context, String command) {
    return runCommand(context, Collections.singletonList(command));
  }

  public ShellResponse runCommand(NodeUniverseContext context, List<String> command) {
    return runCommand(context, command, true /* use bash */);
  }

  /**
   * Runs a command on the node.
   *
   * @param useBash false when the argv is exact and must not be re-parsed by a shell, e.g. quoted
   *     SQL; see {@link #escapeForSshTransport} for how SSH honours it.
   */
  public ShellResponse runCommand(
      NodeUniverseContext context, List<String> command, boolean useBash) {
    Universe universe = context.getUniverse();
    NodeDetails node = context.getNode();
    ShellProcessContext shellProcessContext = context.getShellProcessContext();
    Optional<NodeAgent> optional =
        shellProcessContext.isUseSshConnectionOnly()
            ? Optional.empty()
            : maybeUpgradeAndGetNodeAgent(universe, node);
    if (optional.isPresent()) {
      return nodeActionRunner.runCommand(optional.get(), command, shellProcessContext, useBash);
    }
    List<String> actionArgs = new ArrayList<>();
    if (MapUtils.isNotEmpty(shellProcessContext.getRedactedVals())) {
      actionArgs.add("--skip_cmd_logging");
    }
    actionArgs.add("--command");
    if (fallsBackToSsh(universe.getNodeDeploymentMode(node)) && !useBash) {
      actionArgs.addAll(escapeForSshTransport(command, useBash));
      shellProcessContext = withSshEscapedRedactionKeys(context.getShellProcessContext());
      context = context.toBuilder().shellProcessContext(shellProcessContext).build();
    } else {
      actionArgs.addAll(command);
    }
    return executeNodeAction(UniverseNodeAction.RUN_COMMAND, context, actionArgs);
  }

  /**
   * Whether a command that bypasses node agent goes through ssh.py, the one transport that
   * re-parses argv; kubectl exec, local and unknown don't.
   */
  static boolean fallsBackToSsh(CloudType cloudType) {
    return cloudType != CloudType.kubernetes
        && cloudType != CloudType.local
        && cloudType != Common.CloudType.unknown;
  }

  /**
   * Compensates for ssh.py folding argv back into one command line: it wraps each argument holding
   * a space in single quotes without escaping the quotes inside, so {@code SELECT 'a'} reaches the
   * shell as {@code 'SELECT 'a''}. Pre-escaping those quotes with {@code '"'"'} lets its wrapper
   * rebuild the argument exactly. ssh.py leaves other arguments bare, so one holding a shell
   * character ({@code ;}, {@code $(..)}, a glob, a quote) is quoted here in full, to arrive as node
   * agent passes it. With useBash the caller asked for re-parsing, which is what ssh.py and node
   * agent both do.
   */
  static List<String> escapeForSshTransport(List<String> command, boolean useBash) {
    if (useBash) {
      return command;
    }
    return command.stream().map(NodeUniverseManager::escapeArgForSsh).collect(Collectors.toList());
  }

  private static String escapeArgForSsh(String part) {
    String quotesEscaped = part.replace("'", "'\"'\"'");
    if (part.contains(" ")) {
      return quotesEscaped; // ssh.py adds the enclosing quotes
    }
    return SHELL_SAFE_ARG.matcher(part).matches() ? part : "'" + quotesEscaped + "'";
  }

  /**
   * Redaction matches whole arguments, so after {@link #escapeForSshTransport} rewrites one it has
   * to match the rewritten form too, or a password in it is logged.
   */
  private static ShellProcessContext withSshEscapedRedactionKeys(ShellProcessContext context) {
    if (MapUtils.isEmpty(context.getRedactedVals())) {
      return context;
    }
    Map<String, String> redactedVals = new HashMap<>(context.getRedactedVals());
    context
        .getRedactedVals()
        .forEach((arg, redacted) -> redactedVals.put(escapeArgForSsh(arg), redacted));
    return context.toBuilder().redactedVals(redactedVals).build();
  }

  /**
   * Runs a script on the node to test if the given directory is writable
   *
   * @param directoryPath Full directory path ending in '/'
   * @param node Node on which to test the directory
   * @param universe Universe in which the node exists
   * @return Whether the given directory can be written to.
   */
  public boolean isDirectoryWritable(String directoryPath, NodeDetails node, Universe universe) {
    List<String> actionArgs = new ArrayList<>();
    actionArgs.add("--test_directory");
    actionArgs.add(directoryPath);
    return executeNodeAction(
            UniverseNodeAction.TEST_DIRECTORY, NodeUniverseContext.of(universe, node), actionArgs)
        .getMessage()
        .equals("Directory is writable");
  }

  /**
   * Runs a local script with parameters passed in a list
   *
   * @param context node, universe, and shell settings for the run
   * @param localScriptPath
   * @param params
   * @return the ShellResponse object
   */
  public ShellResponse runScript(
      NodeUniverseContext context, String localScriptPath, List<String> params) {
    Universe universe = context.getUniverse();
    NodeDetails node = context.getNode();
    ShellProcessContext shellProcessContext = context.getShellProcessContext();
    Optional<NodeAgent> optional =
        shellProcessContext.isUseSshConnectionOnly()
            ? Optional.empty()
            : maybeUpgradeAndGetNodeAgent(universe, node);
    if (optional.isPresent()) {
      return nodeActionRunner.runScript(
          optional.get(), localScriptPath, params, shellProcessContext);
    }
    List<String> actionArgs = new ArrayList<>();
    actionArgs.add("--local_script_path");
    actionArgs.add(localScriptPath);
    actionArgs.add("--params");
    actionArgs.addAll(quoteScriptParams(params));
    return executeNodeAction(UniverseNodeAction.RUN_SCRIPT, context, actionArgs);
  }

  /**
   * Quotes script parameters for the run_node_action.py transports (ssh, rpc and k8s), which build
   * a {@code /bin/bash -s <params> <<'EOF'} line and let a shell split it. Node agent already
   * quotes them the same way in {@code NodeAgentClient.executeScript}.
   */
  static List<String> quoteScriptParams(List<String> params) {
    return params.stream().map(NodeUniverseManager::shellQuote).collect(Collectors.toList());
  }

  /** Wraps a value in single quotes, escaping embedded ones with the {@code '\''} idiom. */
  private static String shellQuote(String value) {
    return value == null ? "''" : "'" + value.replace("'", "'\\''") + "'";
  }

  public ShellResponse runYbAdminCommand(
      NodeUniverseContext context, String ybAdminCommand, List<String> args, long timeoutSec) {
    List<String> command = new ArrayList<>();
    command.add(getYbHomeDir(context.getNode(), context.getUniverse()) + "/master/bin/yb-admin");
    command.add("--master_addresses");
    command.add(context.getUniverse().getMasterAddresses());
    UserIntent userIntent = context.getUserIntent();
    if (userIntent.enableNodeToNodeEncrypt) {
      command.add("-certs_dir_name");
      command.add(getCertsDir(context.getUniverse(), context.getNode()));
    }
    command.add("-timeout_ms");
    command.add(String.valueOf(TimeUnit.SECONDS.toMillis(timeoutSec)));
    command.add(ybAdminCommand);
    command.addAll(args);
    ShellProcessContext shellProcessContext =
        ShellProcessContext.builder().logCmdOutput(true).timeoutSecs(timeoutSec).build();
    return runCommand(
        NodeUniverseContext.of(context.getUniverse(), context.getNode(), shellProcessContext),
        command);
  }

  public ShellResponse runYsqlCommand(
      NodeUniverseYsqlContext context, String dbName, String ysqlCommand) {
    Provider provider = Util.getProviderForNode(context.getNode(), context.getUniverse());
    if (provider.getCloudCode() == CloudType.local) {
      return localNodeUniverseManager.runYsqlCommand(
          withDefaultYsqlTimeout(context), dbName, ysqlCommand);
    }
    return runYsqlBatchCommands(context, dbName, List.of(ysqlCommand));
  }

  /**
   * useBash=false: the spec is already a {@code bash -c} script, and a second wrap breaks quoted
   * SQL literals such as the UUIDs in UpdateConsistencyCheck's INSERT.
   */
  public ShellResponse runYsqlBatchCommands(
      NodeUniverseYsqlContext context, String dbName, List<String> ysqlCommands) {
    RemoteBashRunSpec spec = buildYsqlBatchRunSpec(context, dbName, ysqlCommands);
    return runCommand(
        context.toBuilder().shellProcessContext(spec.context).build(),
        spec.command,
        false /* useBash */);
  }

  public ShellResponse runYsqlSupportBundleBatchCommands(
      NodeUniverseYsqlContext context, String dbName, List<String> ysqlCommands) {
    RemoteBashRunSpec spec = buildYsqlSupportBundleBatchRunSpec(context, dbName, ysqlCommands);
    return runCommand(
        context.toBuilder().shellProcessContext(spec.context).build(),
        spec.command,
        false /* useBash */);
  }

  /** Builds the {@code bash -c} ysqlsh command and its context; run it with useBash=false. */
  public RemoteBashRunSpec buildYsqlBatchRunSpec(
      NodeUniverseYsqlContext context, String dbName, List<String> ysqlCommands) {
    List<String> command = new ArrayList<>();
    command.add("bash");
    command.add("-c");
    List<String> bashCommand = new ArrayList<>();
    appendYsqlSslExport(bashCommand, context.getUserIntent());
    bashCommand.addAll(buildYsqlshInvocationArgs(context, dbName));
    for (var ysqlCommand : ysqlCommands) {
      bashCommand.add("-c");
      bashCommand.add("\"" + escapeYsqlQueryForShell(ysqlCommand) + "\"");
    }
    String bashCommandStr = String.join(" ", bashCommand);
    command.add(bashCommandStr);
    return new RemoteBashRunSpec(command, buildYsqlShellContext(bashCommandStr, context));
  }

  /**
   * Builds a support-bundle batch script that prints {@code -- query:} headers, runs each query via
   * ysqlsh, captures stderr, records per-query exit codes, and continues after failures.
   */
  public RemoteBashRunSpec buildYsqlSupportBundleBatchRunSpec(
      NodeUniverseYsqlContext context, String dbName, List<String> ysqlCommands) {
    List<String> command = new ArrayList<>();
    command.add("bash");
    command.add("-c");
    command.add(buildYsqlSupportBundleBatchBashScript(context, dbName, ysqlCommands));
    String bashCommandStr = command.get(2);
    return new RemoteBashRunSpec(command, buildYsqlShellContext(bashCommandStr, context));
  }

  String buildYsqlSupportBundleBatchBashScript(
      NodeUniverseYsqlContext context, String dbName, List<String> ysqlCommands) {
    List<String> sslExport = new ArrayList<>();
    appendYsqlSslExport(sslExport, context.getUserIntent());
    String sslExportPrefix = sslExport.isEmpty() ? "" : sslExport.get(0);
    String ysqlshPrefix = String.join(" ", buildYsqlshInvocationArgs(context, dbName));
    StringBuilder script = new StringBuilder();
    if (StringUtils.isNotEmpty(sslExportPrefix)) {
      script.append(sslExportPrefix).append(' ');
    }
    // Support bundles only ever read. Setting this through PGOPTIONS rather than an extra -c keeps
    // it out of the captured output, and it must stay scoped to this builder: the shared
    // buildYsqlBatchRunSpec serves writers such as UpdateConsistencyCheck.
    script.append(SUPPORT_BUNDLE_READ_ONLY_PGOPTIONS).append(' ');
    for (String ysqlCommand : ysqlCommands) {
      script
          .append("printf '%s\\n' ")
          .append(shellSingleQuote("-- query: " + ysqlCommand))
          .append("; ")
          .append(ysqlshPrefix)
          .append(" -c \"")
          .append(escapeYsqlQueryForShell(ysqlCommand))
          .append(
              "\" 2>&1; _ec=$?; if [ $_ec -ne 0 ]; then printf '%s\\n' \"(exit code $_ec)\"; fi;")
          .append(" printf '\\n'; ");
    }
    script.append("exit 0");
    return script.toString();
  }

  private void appendYsqlSslExport(List<String> bashCommand, UserIntent userIntent) {
    if (userIntent.enableClientToNodeEncrypt && !userIntent.enableYSQLAuth) {
      bashCommand.add("export sslmode=\"require\";");
    }
  }

  private List<String> buildYsqlshInvocationArgs(NodeUniverseYsqlContext context, String dbName) {
    NodeDetails node = context.getNode();
    Universe universe = context.getUniverse();
    List<String> bashCommand = new ArrayList<>();
    String customTmpDirectory = GFlagsUtil.getCustomTmpDirectory(confGetter, node, universe);
    String port =
        String.valueOf(
            context.isCpEnabled() ? node.internalYsqlServerRpcPort : node.ysqlServerRpcPort);
    bashCommand.add(getYbHomeDir(node, universe) + "/tserver/bin/ysqlsh");
    bashCommand.add("-h");
    if (context.isAuthEnabled()) {
      bashCommand.add(getYsqlSocketDirExpression(customTmpDirectory, port));
    } else {
      bashCommand.add(node.cloudInfo.private_ip);
    }
    bashCommand.add("-p");
    bashCommand.add(port);
    bashCommand.add("-U");
    bashCommand.add("yugabyte");
    if (StringUtils.isNotEmpty(dbName)) {
      bashCommand.add("-d");
      bashCommand.add(shellQuote(dbName));
    }
    return bashCommand;
  }

  /**
   * Escapes a YSQL query for {@code ysqlsh -c "..."}: the four characters special inside bash
   * double quotes ({@code \}, {@code `}, {@code "}, {@code $}). Single quotes stay literal, since
   * the {@code '"'"'} idiom here corrupts SQL literals such as the UUIDs in UpdateConsistencyCheck;
   * the SSH transport's own quoting is handled by {@link #escapeForSshTransport}.
   */
  static String escapeYsqlQueryForShell(String ysqlCommand) {
    // Backslash is escaped first so the backslashes added below are not escaped a second time.
    return ysqlCommand
        .replace("\\", "\\\\")
        .replace("`", "\\`")
        .replace("\"", "\\\"")
        .replace("$", "\\$");
  }

  private static String shellSingleQuote(String value) {
    return "'" + value.replace("'", "'\"'\"'") + "'";
  }

  private ShellProcessContext buildYsqlShellContext(
      String bashCommandStr, NodeUniverseYsqlContext context) {
    boolean logCmdOutput = context.isLogCmdOutput();
    Map<String, String> valsToRedact = new HashMap<>();
    if (!logCmdOutput) {
      valsToRedact.put(bashCommandStr, Util.REDACTED_YSQL_QUERY);
    } else if (StringUtils.containsIgnoreCase(bashCommandStr, Util.YSQL_PASSWORD_KEYWORD)) {
      valsToRedact.put(bashCommandStr, Util.redactYsqlQuery(bashCommandStr));
    }
    return withDefaultYsqlTimeout(context).getShellProcessContext().toBuilder()
        .logCmdOutput(logCmdOutput && valsToRedact.isEmpty())
        .redactedVals(valsToRedact)
        .build();
  }

  /** A context without a timeout gets the universe's YSQL timeout, as the local path needs one. */
  private NodeUniverseYsqlContext withDefaultYsqlTimeout(NodeUniverseYsqlContext context) {
    if (context.getShellProcessContext().getTimeoutSecs() > 0) {
      return context;
    }
    return context.withTimeout(
        confGetter.getConfForScope(context.getUniverse(), UniverseConfKeys.ysqlTimeoutSecs));
  }

  /** A {@code bash -c} command and its shell context, to be run with useBash=false. */
  public static final class RemoteBashRunSpec {
    public final List<String> command;
    public final ShellProcessContext context;

    public RemoteBashRunSpec(List<String> command, ShellProcessContext context) {
      this.command = command;
      this.context = context;
    }
  }

  /**
   * Runs support-bundle YCQL queries in one remote bash script. Each query is executed via a
   * separate ycqlsh invocation so failures do not prevent later queries from running.
   */
  public ShellResponse runYcqlSupportBundleBatchCommands(
      NodeUniverseContext context, String keyspace, List<String> ycqlCommands) {
    RemoteBashRunSpec spec = buildYcqlSupportBundleBatchRunSpec(context, keyspace, ycqlCommands);
    return runCommand(
        NodeUniverseContext.of(context.getUniverse(), context.getNode(), spec.context),
        spec.command,
        false /* useBash */);
  }

  /**
   * Builds a support-bundle batch script that prints {@code -- query:} headers, runs each query via
   * ycqlsh, captures stderr, records per-query exit codes, and continues after failures. It passes
   * no credentials: YBA has none for YCQL, so the caller skips universes with YCQL auth on.
   */
  public RemoteBashRunSpec buildYcqlSupportBundleBatchRunSpec(
      NodeUniverseContext context, String keyspace, List<String> ycqlCommands) {
    List<String> command = new ArrayList<>();
    command.add("bash");
    command.add("-c");
    command.add(buildYcqlSupportBundleBatchBashScript(context, keyspace, ycqlCommands));

    return new RemoteBashRunSpec(
        command, context.getShellProcessContext().toBuilder().logCmdOutput(true).build());
  }

  String buildYcqlSupportBundleBatchBashScript(
      NodeUniverseContext context, String keyspace, List<String> ycqlCommands) {
    String ycqlshPrefix = String.join(" ", buildYcqlshBaseArgs(context, keyspace));
    StringBuilder script = new StringBuilder();
    for (String ycqlCommand : ycqlCommands) {
      String escapedQuery = escapeYcqlQueryForShell(ensureYcqlSemicolon(ycqlCommand));
      script
          .append("printf '%s\\n' ")
          .append(shellSingleQuote("-- query: " + ycqlCommand))
          .append("; ")
          .append(ycqlshPrefix)
          .append(" -e \"")
          .append(escapedQuery)
          .append(
              "\" 2>&1; _ec=$?; if [ $_ec -ne 0 ]; then printf '%s\\n' \"(exit code $_ec)\"; fi;")
          .append(" printf '\\n'; ");
    }
    script.append("exit 0");
    return script.toString();
  }

  private List<String> buildYcqlshBaseArgs(NodeUniverseContext context, String keyspace) {
    List<String> bashCommand = new ArrayList<>();
    bashCommand.add(
        getYbHomeDir(context.getNode(), context.getUniverse()) + GFlagsUtil.YCQLSH_PATH);
    if (context.getUserIntent().enableClientToNodeEncrypt) {
      bashCommand.add("--ssl");
    }
    bashCommand.add(context.getNode().cloudInfo.private_ip);
    bashCommand.add(String.valueOf(context.getNode().yqlServerRpcPort));
    if (StringUtils.isNotEmpty(keyspace)) {
      bashCommand.add("-k");
      bashCommand.add(shellQuote(keyspace));
    }
    return bashCommand;
  }

  private static String ensureYcqlSemicolon(String ycqlCommand) {
    String trimmed = ycqlCommand.trim();
    if (trimmed.endsWith(";")) {
      return trimmed;
    }
    return trimmed + ";";
  }

  /**
   * Escapes a YCQL query for embedding in {@code ycqlsh -e "..."}. See {@link
   * #escapeYsqlQueryForShell(String)} for why single quotes are not escaped.
   */
  private static String escapeYcqlQueryForShell(String ycqlCommand) {
    // Backslash is escaped first so the backslashes added below are not escaped a second time.
    return ycqlCommand
        .replace("\\", "\\\\")
        .replace("`", "\\`")
        .replace("\"", "\\\"")
        .replace("$", "\\$");
  }

  // ysqlsh opens <dir>/.s.PGSQL.<port>, so ignore sockets a SIGKILLed postmaster left on another
  // port. Postgres falls back to /tmp when tmp_dir is too long; the newest socket wins.
  static String getYsqlSocketDirExpression(String tmpDirectory, String port) {
    String sockets = tmpDirectory + "/.yb.*/.s.PGSQL." + port;
    if (!"/tmp".equals(tmpDirectory)) {
      sockets += " /tmp/.yb.*/.s.PGSQL." + port;
    }
    return String.format("$(dirname \"$(ls -t %s 2>/dev/null | head -1)\")", sockets);
  }

  /**
   * Returns yb home directory for node
   *
   * @param node
   * @param universe
   * @return home directory
   */
  public String getYbHomeDir(NodeDetails node, Universe universe) {
    Provider provider = Util.getProviderForNode(node, universe);
    return provider.getYbHome();
  }

  /**
   * Placeholder method to get tmp directory for node
   *
   * @return tmp directory
   */
  public String getYbTmpDir() {
    return "/tmp";
  }

  public Optional<NodeAgent> maybeUpgradeAndGetNodeAgent(Universe universe, NodeDetails node) {
    CloudType cloudType = universe.getNodeDeploymentMode(node);
    if (!NodeAgentClient.isCloudTypeSupported(cloudType)) {
      log.debug("Node agent is not supported on provider type {}", cloudType);
      return Optional.empty();
    }
    // Calls hitting here may or may not have node agent. Java client calls already validate the
    // presence of node agents.
    return getNodeAgentClient().maybeGetAndUpgrade(node.cloudInfo.private_ip);
  }

  private void addConnectionParams(
      NodeUniverseContext context, List<String> commandArgs, Map<String, String> redactedVals) {
    Universe universe = context.getUniverse();
    NodeDetails node = context.getNode();
    ShellProcessContext shellProcessContext = context.getShellProcessContext();
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getClusterByUuid(node.placementUuid);
    UserIntent userIntent = context.getUserIntent();
    CloudType cloudType = universe.getNodeDeploymentMode(node);
    UUID providerUUID = cluster.getProviderUUIDForNode(node);
    String accessKeyCode = userIntent.getAccessKeyCodeForProvider(providerUUID);
    if (cloudType == CloudType.kubernetes) {
      Map<String, String> k8sConfig =
          KubernetesUtil.getKubernetesConfigPerPod(
                  cluster.getOverallPlacement(),
                  universe.getUniverseDetails().getNodesInCluster(cluster.uuid))
              .get(node.cloudInfo.private_ip);
      if (k8sConfig == null) {
        throw new RuntimeException("Kubernetes config cannot be null");
      }
      commandArgs.add("k8s");
      commandArgs.add("--k8s_config");
      commandArgs.add(Json.stringify(Json.toJson(k8sConfig)));
    } else if (cloudType != Common.CloudType.unknown) {
      Optional<NodeAgent> optional =
          shellProcessContext.isUseSshConnectionOnly()
              ? Optional.empty()
              : maybeUpgradeAndGetNodeAgent(universe, node);
      if (optional.isPresent()) {
        NodeAgent nodeAgent = optional.get();
        commandArgs.add("rpc");
        getNodeAgentClient().addNodeAgentClientParams(nodeAgent, commandArgs, redactedVals);
      } else if (!StringUtils.isEmpty(accessKeyCode)) {
        Provider provider = Provider.getOrBadRequest(providerUUID);
        AccessKey accessKey = AccessKey.getOrBadRequest(providerUUID, accessKeyCode);
        String sshPort = provider.getDetails().sshPort.toString();
        if (node.sshPortOverride != null) {
          sshPort = node.sshPortOverride.toString();
        } else {
          UUID imageBundleUUID =
              Util.retreiveImageBundleUUID(
                  universe.getUniverseDetails().arch, userIntent, provider);
          if (imageBundleUUID != null) {
            ImageBundle.NodeProperties toOverwriteNodeProperties =
                imageBundleUtil.getNodePropertiesOrFail(
                    imageBundleUUID, node.cloudInfo.region, cloudType.toString());
            sshPort = toOverwriteNodeProperties.getSshPort().toString();
          }
        }
        commandArgs.add("ssh");
        // Default SSH port can be the custom port for custom images.
        if (node.sshPortOverride == null
            && StringUtils.isNotBlank(shellProcessContext.getSshUser())
            && Util.isAddressReachable(node.cloudInfo.private_ip, 22)) {
          // In case the custom ssh User is specified in the context, that will be
          // prepare node stage, where the custom sshPort might not be configured yet.
          sshPort = "22";
        }
        commandArgs.add("--port");
        commandArgs.add(sshPort);
        commandArgs.add("--ip");
        commandArgs.add(node.cloudInfo.private_ip);
        commandArgs.add("--key");
        commandArgs.add(accessKey.getKeyInfo().privateKey);
        if (confGetter.getGlobalConf(GlobalConfKeys.ssh2Enabled)) {
          commandArgs.add("--ssh2_enabled");
        }
      } else {
        String errMsg =
            String.format(
                "No %s connection is available to the node %s",
                context.getShellProcessContext().isUseSshConnectionOnly() ? "SSH" : "remote",
                node.cloudInfo.private_ip);
        log.error(errMsg);
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
      }
      if (StringUtils.isNotBlank(shellProcessContext.getSshUser())) {
        commandArgs.add("--user");
        commandArgs.add(shellProcessContext.getSshUser());
      }
    }
  }

  private ShellResponse executeNodeAction(
      UniverseNodeAction nodeAction, NodeUniverseContext context, List<String> actionArgs) {
    Universe universe = context.getUniverse();
    NodeDetails node = context.getNode();
    ShellProcessContext shellProcessContext = context.getShellProcessContext();
    List<String> commandArgs = new ArrayList<>();
    Map<String, String> redactedVals = new HashMap<>();
    commandArgs.add(PY_WRAPPER);
    commandArgs.add(NODE_ACTION_SSH_SCRIPT);
    if (node.isMaster) {
      commandArgs.add("--is_master");
    }
    commandArgs.add("--node_name");
    commandArgs.add(node.nodeName);
    addConnectionParams(context, commandArgs, redactedVals);
    commandArgs.add(nodeAction.name().toLowerCase());
    commandArgs.addAll(actionArgs);
    if (MapUtils.isNotEmpty(redactedVals)) {
      // Create a new context as a context is immutable.
      if (MapUtils.isNotEmpty(shellProcessContext.getRedactedVals())) {
        redactedVals.putAll(shellProcessContext.getRedactedVals());
      }
      shellProcessContext = shellProcessContext.toBuilder().redactedVals(redactedVals).build();
    }
    Cluster curCluster = universe.getCluster(node.placementUuid);
    CloudType cloudType = curCluster.getProviderCloudType(node);
    if (cloudType == CloudType.local) {
      return localNodeUniverseManager.executeNodeAction(universe, node, nodeAction, commandArgs);
    }
    return shellProcessHandler.run(commandArgs, shellProcessContext);
  }

  private String getCertsDir(Universe universe, NodeDetails node) {
    if (universe.getNodeDeploymentMode(node).equals(Common.CloudType.kubernetes)) {
      return K8S_CERTS_DIR;
    }
    return getYbHomeDir(node, universe) + CERTS_DIR;
  }

  /**
   * Checks if a file or directory exists on the node in the universe using the default shell
   * context (runs as the default SSH user).
   *
   * @param node
   * @param universe
   * @param remotePath
   * @return true if file/directory exists, else false
   */
  public boolean checkNodeIfFileExists(NodeDetails node, Universe universe, String remotePath) {
    return checkNodeIfFileExists(NodeUniverseContext.of(universe, node), remotePath);
  }

  /**
   * Checks if a file or directory exists on the node in the universe under the provided shell
   * context. Callers that supply a non-default linux_user (e.g. the file-collection API) must use
   * this overload so the existence check runs as the same user as the surrounding operation --
   * otherwise `test -e` may report missing for files whose parent directory is not traversable by
   * the default user.
   *
   * @param context node, universe, and shell context controlling the user, timeout, and other
   *     execution settings
   * @param remotePath
   * @return true if file/directory exists, else false
   */
  public boolean checkNodeIfFileExists(NodeUniverseContext context, String remotePath) {
    List<String> params = new ArrayList<>();
    params.add("check_file_exists");
    params.add(remotePath);

    ShellResponse scriptOutput = runScript(context, NODE_UTILS_SCRIPT, params);

    if (!scriptOutput.isSuccess()) {
      throw new RuntimeException(
          String.format("Failed to run command. Got error: '%s'", scriptOutput.getMessage()));
    }

    return scriptOutput.extractRunCommandOutput().trim().equals("1");
  }

  /**
   * Try to run a simple command like ls on the remote node to see if it is responsive. If
   * unresponsive for more than `timeoutSecs`, return false.
   *
   * @param node
   * @param universe
   * @param timeoutSecs
   * @return
   */
  public boolean isNodeReachable(NodeDetails node, Universe universe, long timeoutSecs) {
    List<String> command = new ArrayList<>();
    command.add("echo");
    command.add("'test'");

    ShellResponse response =
        runCommand(NodeUniverseContext.of(universe, node).withTimeout(timeoutSecs), command);

    if (response.isSuccess()) {
      return true;
    }
    log.warn(
        "Node '{}' is unreachable for '{}' sec, or threw an error: '{}'.",
        node.getNodeName(),
        timeoutSecs,
        response.getMessage());
    return false;
  }

  /**
   * Reachability pre-check for per-node platform APIs (run-script, file-collections, ...) that uses
   * the shared {@code yb.node_script.reachability_check_timeout_sec} runtime flag so a single knob
   * controls the timeout across all callers.
   */
  public boolean isNodeReachable(NodeDetails node, Universe universe) {
    long timeoutSecs =
        confGetter.getGlobalConf(GlobalConfKeys.nodeScriptReachabilityCheckTimeoutSec);
    return isNodeReachable(node, universe, timeoutSecs);
  }

  /**
   * Gets a map of all the absolute file paths to sizes at a given remote directory
   *
   * @param node
   * @param universe
   * @param remoteDirPath
   * @param maxDepth
   * @param fileType
   * @return map of absolute file paths to file sizes
   */
  public Map<String, Long> getNodeFilePathAndSizes(
      NodeDetails node, Universe universe, String remoteDirPath, int maxDepth, String fileType) {
    return getNodeFilePathAndSizes(
        NodeUniverseContext.of(universe, node), remoteDirPath, maxDepth, fileType);
  }

  public Map<String, Long> getNodeFilePathAndSizes(
      NodeUniverseContext context, String remoteDirPath, int maxDepth, String fileType) {
    NodeDetails node = context.getNode();
    Universe universe = context.getUniverse();
    String localTempFilePath =
        getLocalTmpDir() + "/" + UUID.randomUUID().toString() + "-source-files-unfiltered.txt";
    String remoteTempFilePath =
        getRemoteTmpDir(node, universe)
            + "/"
            + UUID.randomUUID().toString()
            + "-source-files-unfiltered.txt";

    List<String> findCommandParams = new ArrayList<>();
    findCommandParams.add("get_paths_and_sizes");
    findCommandParams.add(remoteDirPath);
    findCommandParams.add(String.valueOf(maxDepth));
    findCommandParams.add(fileType);
    findCommandParams.add(remoteTempFilePath);

    runScript(context, NODE_UTILS_SCRIPT, findCommandParams).processErrors();
    // Download the files list.
    copyFileFromNode(context, remoteTempFilePath, localTempFilePath);

    // Delete file from remote server after copying to local.
    List<String> removeCommand = new ArrayList<>();
    removeCommand.add("rm");
    removeCommand.add(remoteTempFilePath);
    runCommand(context, removeCommand);

    // Populate the text file into array.
    List<String> nodeFilePathStrings = Arrays.asList();
    // LinkedHashMap to maintain insertion order.
    // Files are ordered most recent to least.
    Map<String, Long> nodeFilePathSizeMap = new LinkedHashMap<>();
    try {
      nodeFilePathStrings = Files.readAllLines(Paths.get(localTempFilePath));
      for (String outputLine : nodeFilePathStrings) {
        String[] outputLineSplit = outputLine.split("\\s+", 2);
        if (!StringUtils.isBlank(outputLine) && outputLineSplit.length == 2) {
          nodeFilePathSizeMap.put(outputLineSplit[1], Long.valueOf(outputLineSplit[0]));
        }
      }
    } catch (IOException e) {
      log.error("Error occurred", e);
    } finally {
      FileUtils.deleteQuietly(new File(localTempFilePath));
    }
    return nodeFilePathSizeMap;
  }

  /**
   * Returns a map of file names to their sizes (in bytes) present in a remote directory on the
   * node. This function creates a temp file with these sizes and names and copies the temp file
   * from remote to local. Then reads and processes this info from the local temp file. This is done
   * so that this operation is scalable for large number of files present on the node.
   *
   * @param nodeUniverseContext
   * @param remoteDirPath
   * @param startDate
   * @param endDate
   * @return a map of filenames to filesizes
   */
  public Map<String, Long> getNodeFilePathsAndSizeWithinDates(
      NodeUniverseContext nodeUniverseContext, String remoteDirPath, Date startDate, Date endDate) {
    String randomUUIDStr = UUID.randomUUID().toString();
    String localTempFilePath =
        getLocalTmpDir() + "/" + randomUUIDStr + "-source-files-and-sizes.txt";
    String remoteTempFilePath =
        getRemoteTmpDir(nodeUniverseContext.getNode(), nodeUniverseContext.getUniverse())
            + "/"
            + randomUUIDStr
            + "-source-files-and-sizes.txt";

    SimpleDateFormat formatter = new SimpleDateFormat("yyyy-MM-dd");
    List<String> findCommandParams = new ArrayList<>();
    findCommandParams.add("get_paths_and_sizes_within_dates");
    findCommandParams.add(remoteDirPath);
    findCommandParams.add(remoteTempFilePath);
    findCommandParams.add(formatter.format(startDate));
    findCommandParams.add(formatter.format(DateUtils.addDays(endDate, 1)));

    runScript(nodeUniverseContext, NODE_UTILS_SCRIPT, findCommandParams).processErrors();
    // Download the files list.
    copyFileFromNode(
        nodeUniverseContext.getNode(),
        nodeUniverseContext.getUniverse(),
        remoteTempFilePath,
        localTempFilePath);

    // Delete file from remote server after copying to local.
    List<String> removeCommand = new ArrayList<>();
    removeCommand.add("rm");
    removeCommand.add(remoteTempFilePath);
    runCommand(nodeUniverseContext, removeCommand);

    // Populate the text file into array.
    List<String> nodeFilePathStrings = Arrays.asList();
    // LinkedHashMap to maintain insertion order.
    // Files are ordered most recent to least.
    Map<String, Long> nodeFilePathSizeMap = new LinkedHashMap<>();
    try {
      nodeFilePathStrings = Files.readAllLines(Paths.get(localTempFilePath));
      log.debug(
          "List of files found on the node '{}': '{}'",
          nodeUniverseContext.getNode().nodeName,
          nodeFilePathStrings);
      for (String outputLine : nodeFilePathStrings) {
        String[] outputLineSplit = outputLine.split("\\s+", 2);
        if (!StringUtils.isBlank(outputLine) && outputLineSplit.length == 2) {
          nodeFilePathSizeMap.put(outputLineSplit[1], Long.valueOf(outputLineSplit[0]));
        }
      }
    } catch (IOException e) {
      log.error("Error occurred", e);
    } finally {
      FileUtils.deleteQuietly(new File(localTempFilePath));
    }
    return nodeFilePathSizeMap;
  }

  public void postProcessInMemoryGFlags(
      Map<String, String> gflags, Universe universe, NodeDetails nodeDetails) {
    if (universe
        .getCluster(nodeDetails.placementUuid)
        .userIntent
        .getAllCloudTypes()
        .contains(CloudType.local)) {
      localNodeUniverseManager.postProcessGFlagsMap(gflags, universe, nodeDetails);
    }
  }

  public enum UniverseNodeAction {
    RUN_COMMAND,
    RUN_SCRIPT,
    DOWNLOAD_LOGS,
    DOWNLOAD_FILE,
    COPY_FILE,
    UPLOAD_FILE,
    TEST_DIRECTORY,
    BULK_CHECK_FILES_EXIST
  }
}
