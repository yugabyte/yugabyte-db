// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.common.collect.ImmutableSet;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2Manifest;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestLoader;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.ScriptTarComponentSpec;
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
 * <p>Script specs are pinned to the server's own manifest rather than to a list here, since the
 * scripts run {@code params[0]} as a command: see {@link #validateManifestEntrypoints}.
 */
@Slf4j
@Singleton
public class SupportBundleV2SpecValidator {

  /** The only scripts the shipped manifest ever references. */
  static final Set<String> ALLOWED_SCRIPT_FILE_NAMES =
      ImmutableSet.of("support_bundle_utils.sh", "yba_utils.sh");

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
   * Constructs that pass the leading keyword check but still reach outside the database or change
   * it. ysqlsh connects as a superuser, and a read-only transaction stops neither file reads nor
   * the side effects of a function call.
   */
  static final Pattern DENIED_SQL_CONSTRUCTS =
      Pattern.compile(
          String.join(
              "|",
              // Server files and programs; COPY ... TO PROGRAM runs a shell command.
              "\\b(pg_read_file|pg_read_binary_file|pg_stat_file|pg_ls_\\w+|pg_file_\\w+"
                  + "|pg_logdir_ls|lo_\\w+|pg_read_server_files|pg_write_server_files"
                  + "|pg_execute_server_program)\\b",
              "\\bcopy\\b[\\s\\S]*\\bprogram\\b",
              // SQL run from a string, which the lexer blanks: query_to_xml, table_to_xml, ...
              "\\b\\w*_to_xml\\w*\\b",
              "\\b(dblink\\w*|set_config)\\b",
              // Credentials, large objects and sampled column values.
              "\\b(pg_authid|pg_shadow|pg_user_mappings?|pg_largeobject\\w*|pg_statistic\\w*"
                  + "|pg_stats(_ext\\w*)?)\\b",
              // Side effects on sessions, configuration, statistics, replication and catalogs.
              "\\b(pg_terminate_backend|pg_cancel_backend|pg_reload_conf|pg_rotate_logfile"
                  + "|pg_promote|pg_switch_wal|pg_wal_replay_\\w+|pg_create_\\w+"
                  + "|pg_drop_replication_slot|pg_copy_\\w+_slot|pg_replication_slot_advance"
                  + "|pg_logical_slot_\\w+|pg_replication_origin_\\w+|pg_(start|stop)_backup"
                  + "|pg_backup_(start|stop)|pg_advisory\\w*|pg_try_advisory\\w*|pg_sleep\\w*"
                  + "|pg_notify|pg_import_system_collations)\\b",
              "\\b\\w*reset\\w*\\s*\\(",
              "\\b(yb_cancel_\\w+|yb_query_diagnostics|yb_log_\\w+|yb_fix_\\w+|yb_increment_\\w+"
                  + "|yb_binary_upgrade_\\w+|yb_pg_stat_plans_(insert|read_file|write_file)"
                  + "|yb_replication_origin_\\w+|yb_xcluster_set_\\w+|yb_index_check)\\b"),
          Pattern.CASE_INSENSITIVE);

  /** {@code U&'...'} and {@code U&"..."} spell names with escapes the deny list can't read. */
  static final Pattern UNICODE_ESCAPE = Pattern.compile("(?<![\\w$])[uU]&['\"]");

  /** A single path segment: no separators, no traversal, nothing the shell would reinterpret. */
  static final Pattern SAFE_FILE_NAME = Pattern.compile("[A-Za-z0-9._-]{1,128}");

  /** Characters permitted in a remote tar path once the per run tokens are substituted out. */
  static final Pattern UNSAFE_PATH_CHARACTER = Pattern.compile("[^A-Za-z0-9._/-]");

  /** A Postgres database name the shell can't reinterpret; {@code $} is out because it expands. */
  static final Pattern SAFE_DB_NAME = Pattern.compile("[A-Za-z_][A-Za-z0-9_-]{0,62}");

  static final Pattern SAFE_KEYSPACE = Pattern.compile("[A-Za-z_][A-Za-z0-9_]{0,47}");

  private final Config staticConf;
  private final RuntimeConfGetter confGetter;
  private final SupportBundleV2ManifestLoader manifestLoader;

  @Inject
  public SupportBundleV2SpecValidator(
      Config staticConf,
      RuntimeConfGetter confGetter,
      SupportBundleV2ManifestLoader manifestLoader) {
    this.staticConf = staticConf;
    this.confGetter = confGetter;
    this.manifestLoader = manifestLoader;
  }

  /**
   * Requires each script spec to be what the server's manifest configures for its component: the
   * component must belong to {@code group}, {@code params[0]} must be its entrypoint and {@code
   * scriptPath} its script file. Both scripts run {@code params[0]} through {@code "$@"}, so this
   * is what decides what executes on a DB node or the YBA host. The CLI builds all three values
   * from its copy of the same manifest, so a mismatch means the two copies differ.
   */
  public void validateManifestEntrypoints(
      String group, List<? extends ScriptTarComponentSpec> specs) {
    SupportBundleV2Manifest.Spec manifest = manifestLoader.load().getSpec();
    List<String> members =
        manifest.getNodeComponents().getOrDefault(group, manifest.getGlobalComponents().get(group));
    if (members == null) {
      members = List.of();
    }
    Path devopsHome = devopsHome();
    for (ScriptTarComponentSpec spec : specs) {
      String name = spec.getComponentName();
      if (StringUtils.isBlank(name) || !members.contains(name)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "'%s' is not a %s component in the support bundle manifest. Valid names: %s",
                name, group, sorted(members)));
      }
      SupportBundleV2Manifest.Component config = manifest.getComponentConfigs().get(name);
      if (config == null
          || StringUtils.isBlank(config.getScript().getEntrypoint())
          || StringUtils.isBlank(config.getScript().getScriptFile())) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            String.format(
                "Support bundle manifest %s lists '%s' under %s but configures no script for it",
                manifestLoader.manifestPath(), name, group));
      }
      String entrypoint = config.getScript().getEntrypoint();
      List<String> params = spec.getParams();
      String requested = params == null || params.isEmpty() ? null : params.get(0);
      if (!entrypoint.equals(requested)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Component '%s' must run the manifest entrypoint '%s' as params[0], not '%s'.",
                name, entrypoint, requested));
      }
      String scriptFile = config.getScript().getScriptFile();
      if (!resolveScript(devopsHome, scriptFile)
          .equals(resolveScript(devopsHome, spec.getScriptPath()))) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Component '%s' must run the manifest script '%s', not '%s'.",
                name, scriptFile, spec.getScriptPath()));
      }
    }
  }

  /**
   * Confines the script to one of the shipped bundle scripts under {@code yb.devops.home}.
   *
   * <p>Both the node components and {@code AbstractScriptTarYbaComponent} run this path with {@code
   * /bin/bash}, the latter directly on the YBA host, so an unconstrained value here is remote code
   * execution on the control plane.
   */
  public void validateScriptPath(String componentName, String scriptPath) {
    Path devopsHome = devopsHome();
    Path resolved = resolveScript(devopsHome, scriptPath);

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
        remoteTarPath
            .replace(SupportBundleSpecTokenSupport.NODE_NAME_TOKEN, "nodeName")
            .replace(SupportBundleSpecTokenSupport.BUNDLE_UUID_TOKEN, "bundleUuid");
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

  /** ysqlsh gets the name unquoted inside a {@code bash -c} script. Blank means the default. */
  public void validateDbName(String componentName, String dbName) {
    validateIdentifier(componentName, "dbName", dbName, SAFE_DB_NAME, "[A-Za-z0-9_-], up to 63");
  }

  /** ycqlsh gets the keyspace unquoted inside a {@code bash -c} script. Blank means none. */
  public void validateKeyspace(String componentName, String keyspace) {
    validateIdentifier(
        componentName, "keyspace", keyspace, SAFE_KEYSPACE, "[A-Za-z0-9_], up to 48");
  }

  private static void validateIdentifier(
      String componentName, String fieldName, String value, Pattern pattern, String rule) {
    if (StringUtils.isBlank(value) || pattern.matcher(value).matches()) {
      return;
    }
    throw new PlatformServiceException(
        BAD_REQUEST,
        String.format(
            "Component '%s' has an invalid '%s' = '%s'. It must start with a letter or '_' and"
                + " use only %s characters.",
            componentName, fieldName, value, rule));
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

  /** Pins every spec's user to yugabyte; the provider user comes only from the manifest's runAs. */
  public void validateLinuxUser(String componentName, String linuxUser, Universe universe) {
    if (universe != null) {
      Util.validateLinuxUserForOnPrem(linuxUser, universe);
    }
    if (StringUtils.isBlank(linuxUser)) {
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

    if (UNICODE_ESCAPE.matcher(query).find()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Component '%s' has a %s query with a Unicode escape (U&) string or name: '%s'.",
              componentName, language, query));
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

  private Path devopsHome() {
    return canonicalize(Paths.get(staticConf.getString("yb.devops.home")));
  }

  /** A relative script path is relative to {@code yb.devops.home}, as the components run it. */
  private static Path resolveScript(Path devopsHome, String scriptPath) {
    Path candidate = Paths.get(scriptPath);
    return canonicalize(candidate.isAbsolute() ? candidate : devopsHome.resolve(scriptPath));
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
   * Blanks string literals and comments, and keeps the names inside quoted identifiers, so the rest
   * can be scanned for statement structure and denied names. It follows PostgreSQL's quoting: an
   * {@code E'...'} string takes backslash escapes and {@code $tag$...$tag$} quotes everything up to
   * the same tag. Ending either one at the wrong place would hide the code after it.
   */
  static String stripLiteralsAndComments(String sql) {
    StringBuilder out = new StringBuilder(sql.length());
    int i = 0;
    int length = sql.length();
    while (i < length) {
      char current = sql.charAt(i);
      int dollarTagEnd = current == '$' ? dollarTagEnd(sql, i) : -1;
      if (current == '\'') {
        i = skipString(sql, i, isEscapeStringPrefix(sql, i));
        out.append(' ');
      } else if (current == '"') {
        int end = skipQuoted(sql, i, '"');
        out.append(' ')
            .append(identifierText(sql.substring(i + 1, Math.max(i + 1, end - 1))))
            .append(' ');
        i = end;
      } else if (dollarTagEnd > 0) {
        String tag = sql.substring(i, dollarTagEnd);
        int close = sql.indexOf(tag, dollarTagEnd);
        i = close < 0 ? length : close + tag.length();
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

  /** An {@code E} or {@code e} right before the quote, not ending a longer name. */
  private static boolean isEscapeStringPrefix(String sql, int quoteIndex) {
    return quoteIndex > 0
        && (sql.charAt(quoteIndex - 1) == 'E' || sql.charAt(quoteIndex - 1) == 'e')
        && (quoteIndex < 2 || !isIdentifierChar(sql.charAt(quoteIndex - 2)));
  }

  /** Returns the index just past a single-quoted string's closing quote. */
  private static int skipString(String sql, int openIndex, boolean backslashEscapes) {
    int i = openIndex + 1;
    int length = sql.length();
    while (i < length) {
      char c = sql.charAt(i);
      if (backslashEscapes && c == '\\') {
        i += 2;
      } else if (c == '\'' && i + 1 < length && sql.charAt(i + 1) == '\'') {
        i += 2;
      } else if (c == '\'') {
        return i + 1;
      } else {
        i++;
      }
    }
    return length;
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

  /**
   * Index just past the opening {@code $tag$} at {@code i}, or -1 when none starts there: a {@code
   * $} inside a name, or {@code $1}, is not a dollar quote.
   */
  private static int dollarTagEnd(String sql, int i) {
    if (i > 0 && isIdentifierChar(sql.charAt(i - 1))) {
      return -1;
    }
    int j = i + 1;
    if (j < sql.length() && Character.isDigit(sql.charAt(j))) {
      return -1;
    }
    while (j < sql.length() && (Character.isLetterOrDigit(sql.charAt(j)) || sql.charAt(j) == '_')) {
      j++;
    }
    return j < sql.length() && sql.charAt(j) == '$' ? j + 1 : -1;
  }

  private static boolean isIdentifierChar(char c) {
    return Character.isLetterOrDigit(c) || c == '_' || c == '$';
  }

  /**
   * A quoted name as it reads unquoted; other characters become spaces so ';' stays a separator.
   */
  private static String identifierText(String quoted) {
    return quoted.replace("\"\"", "\"").replaceAll("[^A-Za-z0-9_$.]", " ");
  }

  private static String sorted(Collection<String> values) {
    return values.stream().sorted().collect(java.util.stream.Collectors.joining(", "));
  }
}
