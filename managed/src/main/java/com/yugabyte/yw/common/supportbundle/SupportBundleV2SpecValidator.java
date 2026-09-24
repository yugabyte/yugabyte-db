// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.collect.ImmutableSet;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Universe;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Collection;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Validates caller supplied support bundle v2 specs against allow lists before anything is
 * persisted or executed.
 *
 * <p>Specs stay free form on the wire, so the API is the gate. The lists below mirror what {@code
 * devops/bin/support_bundlev2_manifest.yaml} can produce, which is the complete set of inputs the
 * CLI generates; anything outside it is rejected with a 400.
 *
 * <p>Allow lists rather than deny lists are used wherever the legitimate set is small and known,
 * because a deny list fails open on anything added later. Where that set grows over time it is kept
 * extensible so that adding to it does not mean editing this class, as the yb-admin list is through
 * runtime config. Queries cannot be enumerated at all, so they are gated on the leading statement
 * keyword and then scanned for the read shaped constructs that keyword alone does not catch.
 *
 * <p>Script entrypoints are deliberately not validated here. The scripts dispatch on {@code
 * params[0]} with {@code "$@"}, which runs it as a command, so the caller chooses what executes on
 * the node and on the YBA host. That is accepted as the caller already holds the permission to
 * create support bundles.
 */
@Slf4j
@Singleton
public class SupportBundleV2SpecValidator {

  /** Collects system logs as the provider SSH user with sudo, so it is exempt from the user pin. */
  static final String SYSTEM_LOGS_COMPONENT = "SystemLogs";

  /** The only scripts the shipped manifest ever references. */
  static final Set<String> ALLOWED_SCRIPT_FILE_NAMES =
      ImmutableSet.of("node_utils.sh", "yba_utils.sh");

  /**
   * Read only yb-admin subcommands, taken from the {@code REGISTER_COMMAND} list in {@code
   * src/yb/tools/yb-admin_cli.cc}. The other registered subcommands mutate cluster state and are
   * deliberately absent. Extend through {@code yb.support_bundle.extra_yb_admin_commands} rather
   * than by widening this set.
   */
  static final Set<String> READ_ONLY_YB_ADMIN_COMMANDS =
      ImmutableSet.of(
          "dump_masters_state",
          "dump_sys_catalog_entries",
          "get_auto_flags_config",
          "get_change_data_stream_info",
          "get_is_load_balancer_idle",
          "get_leader_blacklist_completion",
          "get_load_balancer_state",
          "get_load_move_completion",
          "get_replication_status",
          "get_table_hash",
          "get_universe_config",
          "get_universe_replication_info",
          "get_wal_retention_secs",
          "get_xcluster_info",
          "get_xcluster_outbound_replication_group_info",
          "get_xcluster_safe_time",
          "get_ysql_major_version_catalog_state",
          "is_encryption_enabled",
          "is_tablet_splitting_complete",
          "is_xcluster_bootstrap_required",
          "list_all_masters",
          "list_all_tablet_servers",
          "list_cdc_streams",
          "list_change_data_streams",
          "list_clones",
          "list_leader_counts",
          "list_namespaces",
          "list_replica_type_counts",
          "list_snapshot_restorations",
          "list_snapshot_schedules",
          "list_snapshots",
          "list_tablet_server_log_locations",
          "list_tablet_servers",
          "list_tables",
          "list_tables_with_db_types",
          "list_tablets",
          "list_tablets_for_tablet_server",
          "list_universe_replications",
          "list_xcluster_outbound_replication_groups");

  /** Statements that only read. Anything else is rejected before it reaches ysqlsh. */
  static final Set<String> ALLOWED_YSQL_LEADING_KEYWORDS =
      ImmutableSet.of("SELECT", "WITH", "EXPLAIN", "SHOW", "TABLE", "VALUES");

  static final Set<String> ALLOWED_YCQL_LEADING_KEYWORDS =
      ImmutableSet.of("SELECT", "DESCRIBE", "DESC", "SHOW", "LIST");

  /**
   * Constructs that pass the leading keyword check but still reach outside the database. {@code
   * COPY ... TO PROGRAM} is the important one: it is a shell execution primitive available to a
   * superuser, which is what ysqlsh connects as.
   */
  static final Pattern DENIED_SQL_CONSTRUCTS =
      Pattern.compile(
          "\\b(pg_read_file|pg_read_binary_file|pg_stat_file|pg_ls_dir|pg_ls_logdir|pg_ls_waldir"
              + "|lo_import|lo_export|dblink|dblink_exec|pg_terminate_backend|pg_cancel_backend"
              + "|pg_reload_conf|pg_rotate_logfile|pg_read_server_files|pg_write_server_files"
              + "|pg_execute_server_program)\\b|\\bcopy\\b[\\s\\S]*\\bprogram\\b",
          Pattern.CASE_INSENSITIVE);

  /** A single path segment: no separators, no traversal, nothing the shell would reinterpret. */
  static final Pattern SAFE_FILE_NAME = Pattern.compile("[A-Za-z0-9._-]{1,128}");

  /** Characters permitted in a remote tar path once the per run tokens are substituted out. */
  static final Pattern UNSAFE_PATH_CHARACTER = Pattern.compile("[^A-Za-z0-9._/-]");

  /** Per run tokens that the server substitutes in a remote tar path before it reaches a shell. */
  static final String NODE_NAME_TOKEN = "${nodeName}";

  static final String BUNDLE_UUID_TOKEN = "${bundleUuid}";

  private final Config staticConf;
  private final RuntimeConfGetter confGetter;

  @Inject
  public SupportBundleV2SpecValidator(Config staticConf, RuntimeConfGetter confGetter) {
    this.staticConf = staticConf;
    this.confGetter = confGetter;
  }

  /**
   * Confines the script to one of the shipped bundle scripts under {@code yb.devops.home}.
   *
   * <p>Both the node components and {@code AbstractScriptTarYbaComponent} run this path with {@code
   * /bin/bash}, the latter directly on the YBA host, so an unconstrained value here is remote code
   * execution on the control plane.
   */
  public void validateScriptPath(String componentName, String scriptPath) {
    Path devopsHome = canonicalize(Paths.get(staticConf.getString("yb.devops.home")));
    Path candidate = Paths.get(scriptPath);
    Path resolved =
        canonicalize(candidate.isAbsolute() ? candidate : devopsHome.resolve(scriptPath));

    Path fileName = resolved.getFileName();
    boolean allowedName =
        fileName != null && ALLOWED_SCRIPT_FILE_NAMES.contains(fileName.toString());
    if (!allowedName || !resolved.startsWith(devopsHome)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Component '%s' has an unsupported 'scriptPath' = '%s'. Allowed support bundle"
                  + " scripts are %s, resolved under the platform devops directory.",
              componentName, scriptPath, sorted(ALLOWED_SCRIPT_FILE_NAMES)));
    }
  }

  /**
   * Requires an absolute, traversal free path built only from characters that survive a shell
   * unchanged. The {@code ${nodeName}} and {@code ${bundleUuid}} tokens are substituted server side
   * per node, so they are allowed through.
   */
  public void validateRemoteTarPath(String componentName, String remoteTarPath) {
    String detokenized =
        remoteTarPath.replace(NODE_NAME_TOKEN, "nodeName").replace(BUNDLE_UUID_TOKEN, "bundleUuid");
    Matcher unsafe = UNSAFE_PATH_CHARACTER.matcher(detokenized);
    if (!detokenized.startsWith("/") || detokenized.contains("..") || unsafe.find()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Component '%s' has an invalid 'remoteTarPath' = '%s'. It must be an absolute path"
                  + " without '..' and may only contain letters, digits, '.', '_', '-', '/' and"
                  + " the ${nodeName} / ${bundleUuid} tokens.",
              componentName, remoteTarPath));
    }
  }

  /**
   * Requires a single path segment. Applies to both {@code outputFileName} and {@code
   * componentName}, because the components fall back to the component name when no output file name
   * is supplied and both end up resolved against the bundle directory.
   */
  public void validateFileNameSegment(String componentName, String fieldName, String value) {
    if (StringUtils.isBlank(value)) {
      return;
    }
    if (!SAFE_FILE_NAME.matcher(value).matches() || ".".equals(value) || "..".equals(value)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Component '%s' has an invalid '%s' = '%s'. It must be a single file name of up to"
                  + " 128 characters from [A-Za-z0-9._-].",
              componentName, fieldName, value));
    }
  }

  public void validateYbAdminCommands(String componentName, Collection<String> ybAdminCommands) {
    if (ybAdminCommands == null) {
      return;
    }
    Set<String> allowed = allowedYbAdminCommands();
    for (String command : ybAdminCommands) {
      if (StringUtils.isBlank(command) || !allowed.contains(command.trim())) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Component '%s' requested the non read-only or unknown yb-admin command '%s'."
                    + " Support bundles may only run read-only yb-admin commands.",
                componentName, command));
      }
    }
  }

  public void validateYsqlQueries(String componentName, Collection<String> queries) {
    validateQueries(componentName, "YSQL", queries, ALLOWED_YSQL_LEADING_KEYWORDS);
  }

  public void validateYcqlQueries(String componentName, Collection<String> queries) {
    validateQueries(componentName, "YCQL", queries, ALLOWED_YCQL_LEADING_KEYWORDS);
  }

  /**
   * Pins the script user to yugabyte. {@code SystemLogs} is the reviewed exception: it needs the
   * provider SSH user and sudo to read {@code /var/log}, and already ignores the spec value in
   * favour of the provider user.
   */
  public void validateLinuxUser(String componentName, String linuxUser, Universe universe) {
    if (universe != null) {
      Util.validateLinuxUserForOnPrem(linuxUser, universe);
    }
    if (SYSTEM_LOGS_COMPONENT.equals(componentName) || StringUtils.isBlank(linuxUser)) {
      return;
    }
    if (!NodeManager.YUGABYTE_USER.equals(linuxUser)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Component '%s' requested 'linuxUser' = '%s'. Support bundle components may only run"
                  + " as '%s'.",
              componentName, linuxUser, NodeManager.YUGABYTE_USER));
    }
  }

  private void validateQueries(
      String componentName,
      String language,
      Collection<String> queries,
      Set<String> allowedLeading) {
    if (queries == null) {
      return;
    }
    for (String query : queries) {
      validateQuery(componentName, language, query, allowedLeading);
    }
  }

  private void validateQuery(
      String componentName, String language, String query, Set<String> allowedLeading) {
    if (StringUtils.isBlank(query)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format("Component '%s' has a blank %s query.", componentName, language));
    }

    // Literals and comments are removed first so that a ';' or a banned identifier hiding inside a
    // string does not change how the rest of the statement is read.
    String bare = stripLiteralsAndComments(query).trim();
    String withoutTrailingSemicolons = bare.replaceAll("[;\\s]+$", "");
    if (withoutTrailingSemicolons.contains(";")) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Component '%s' has a %s query with more than one statement: '%s'. Provide one"
                  + " statement per list entry.",
              componentName, language, query));
    }

    String leadingKeyword = leadingKeyword(withoutTrailingSemicolons);
    if (!allowedLeading.contains(leadingKeyword)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Component '%s' has a non read-only %s query starting with '%s': '%s'. Only %s"
                  + " statements are allowed.",
              componentName, language, leadingKeyword, query, sorted(allowedLeading)));
    }

    Matcher denied = DENIED_SQL_CONSTRUCTS.matcher(withoutTrailingSemicolons);
    if (denied.find()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Component '%s' has a %s query using the disallowed construct '%s': '%s'.",
              componentName, language, denied.group(), query));
    }
  }

  private Set<String> allowedYbAdminCommands() {
    Set<String> allowed = new LinkedHashSet<>(READ_ONLY_YB_ADMIN_COMMANDS);
    List<String> extra = confGetter.getGlobalConf(GlobalConfKeys.supportBundleExtraYbAdminCommands);
    if (extra != null) {
      extra.stream().filter(StringUtils::isNotBlank).map(String::trim).forEach(allowed::add);
    }
    return allowed;
  }

  /**
   * Resolves symlinks so that containment checks compare like with like. Falls back to plain
   * normalization when the path does not exist yet, which keeps the check meaningful without
   * depending on the file being present.
   */
  private static Path canonicalize(Path path) {
    try {
      return path.toRealPath();
    } catch (Exception e) {
      return path.toAbsolutePath().normalize();
    }
  }

  private static String leadingKeyword(String statement) {
    String trimmed = statement.trim();
    int start = 0;
    while (start < trimmed.length()
        && (trimmed.charAt(start) == '(' || Character.isWhitespace(trimmed.charAt(start)))) {
      start++;
    }
    int end = start;
    while (end < trimmed.length()
        && (Character.isLetter(trimmed.charAt(end)) || trimmed.charAt(end) == '_')) {
      end++;
    }
    return trimmed.substring(start, end).toUpperCase(Locale.ROOT);
  }

  /**
   * Replaces string literals, quoted identifiers and comments with a single space so the remaining
   * text can be scanned for statement structure.
   */
  static String stripLiteralsAndComments(String sql) {
    StringBuilder out = new StringBuilder(sql.length());
    int i = 0;
    int length = sql.length();
    while (i < length) {
      char current = sql.charAt(i);
      if (current == '\'' || current == '"') {
        i = skipQuoted(sql, i, current);
        out.append(' ');
      } else if (current == '-' && i + 1 < length && sql.charAt(i + 1) == '-') {
        while (i < length && sql.charAt(i) != '\n') {
          i++;
        }
        out.append(' ');
      } else if (current == '/' && i + 1 < length && sql.charAt(i + 1) == '*') {
        i += 2;
        while (i + 1 < length && !(sql.charAt(i) == '*' && sql.charAt(i + 1) == '/')) {
          i++;
        }
        i = Math.min(i + 2, length);
        out.append(' ');
      } else {
        out.append(current);
        i++;
      }
    }
    return out.toString();
  }

  /** Returns the index just past the closing quote, treating a doubled quote as an escape. */
  private static int skipQuoted(String sql, int openIndex, char quote) {
    int i = openIndex + 1;
    int length = sql.length();
    while (i < length) {
      if (sql.charAt(i) == quote) {
        if (i + 1 < length && sql.charAt(i + 1) == quote) {
          i += 2;
          continue;
        }
        return i + 1;
      }
      i++;
    }
    return length;
  }

  private static String sorted(Collection<String> values) {
    return values.stream().sorted().collect(java.util.stream.Collectors.joining(", "));
  }
}
