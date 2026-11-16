// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2.nodebin;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2Manifest;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestLoader;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Base64;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

/**
 * Deploys yb-redact-server-conf to DB nodes and supplies redaction params for bundle scripts.
 *
 * <p>server.conf is redacted only on the node, before the tar is built, so an unredacted copy never
 * leaves it. A copy the node can't redact -- the helper is unavailable or fails on it -- is deleted
 * there and reported as {@code REDACTION_FAILURE}; {@code AbstractScriptTarNodeComponent} logs
 * those and reads the {@code REDACTION_STATUS} marker.
 *
 * <p>Components opt in with {@code redactServerConf} in YBA's manifest: GFlags and YbcLogs. The
 * other file-list entrypoints can redact too, but unmarked they get no helper and drop any {@code
 * server.conf} they stage.
 */
@Slf4j
@Singleton
public class SupportBundleServerConfRedactionSupport {

  public static final String REDACT_BINARY_NAME = "yb-redact-server-conf";

  /**
   * Tells support_bundle_utils.sh to drop every staged server.conf: no verified helper to redact
   * it.
   */
  static final String DROP_SERVER_CONF_PARAM = "--drop_server_conf";

  /** Prefix of the error lines YBA logs for each server.conf, or component, not collected. */
  public static final String REDACTION_FAILURE = "REDACTION_FAILURE";

  /** {@code REDACTION_STATUS=} marker: the node redacted every server.conf it staged. */
  static final String REDACTION_STATUS_NODE = "NODE";

  /** {@code REDACTION_STATUS=} marker: the node dropped one or more it could not redact. */
  static final String REDACTION_STATUS_FAILED = "FAILED";

  /** {@code REDACTION_STATUS=} marker: the node staged no server.conf, so nothing to redact. */
  static final String REDACTION_STATUS_NOT_APPLICABLE = "NOT_APPLICABLE";

  private final SupportBundleNodeBinarySupport binarySupport;
  private final GFlagsValidation gFlagsValidation;
  private final SupportBundleV2ManifestLoader manifestLoader;

  /**
   * The sensitive-gflag list, per bundle and DB version: reading it parses the release's gflag
   * metadata, and every redacting component on every node needs it. Per bundle rather than global
   * because a failed read falls back to a one-entry list, which must not outlive the bundle.
   */
  private final Map<UUID, Map<String, String>> sensitiveGflagsB64ByBundle =
      new ConcurrentHashMap<>();

  @Inject
  SupportBundleServerConfRedactionSupport(
      SupportBundleNodeBinarySupport binarySupport,
      GFlagsValidation gFlagsValidation,
      SupportBundleV2ManifestLoader manifestLoader) {
    this.binarySupport = binarySupport;
    this.gFlagsValidation = gFlagsValidation;
    this.manifestLoader = manifestLoader;
  }

  /** Whether YBA's manifest marks the component {@code redactServerConf}. */
  public boolean isServerConfRedactComponent(String componentLabel) {
    SupportBundleV2Manifest.Component config =
        manifestLoader.load().getSpec().getComponentConfigs().get(componentLabel);
    return config != null && config.isRedactServerConf();
  }

  /** Drops the bundle's cached sensitive-gflag lists. */
  public void forgetBundle(UUID bundleUuid) {
    if (bundleUuid != null) {
      sensitiveGflagsB64ByBundle.remove(bundleUuid);
    }
  }

  /**
   * Whether the script reported a redaction outcome. Without one, YBA can't tell whether the tar
   * holds an unredacted server.conf, so the caller must not copy it off the node.
   */
  public static boolean isKnownRedactionStatus(String redactionStatus) {
    return REDACTION_STATUS_NODE.equals(redactionStatus)
        || REDACTION_STATUS_FAILED.equals(redactionStatus)
        || REDACTION_STATUS_NOT_APPLICABLE.equals(redactionStatus);
  }

  String remoteBinaryPath(NodeDetails node, Universe universe) {
    return binarySupport.remoteBinaryPath(REDACT_BINARY_NAME, node, universe);
  }

  /**
   * Uploads the redactor when the node lacks it or holds a different build. Keeping the node copy
   * in step with YBA matters more here than for the other helpers: a node running older redaction
   * logic still reports {@code REDACTION_STATUS=NODE}, which the collector trusts without
   * re-checking.
   */
  public boolean ensureRedactionBinaryOnNode(
      UUID bundleUuid, NodeDetails node, Universe universe, ShellProcessContext context) {
    return binarySupport.ensureBinaryOnNode(
        bundleUuid,
        REDACT_BINARY_NAME,
        node,
        universe,
        context,
        "server.conf will be dropped on the node",
        SupportBundleNodeBinarySupport.UnverifiedNodeCopy.REJECT);
  }

  /** Params for the node-side redaction path, pointing the script at the deployed binary. */
  public List<String> appendRedactionScriptParams(
      UUID bundleUuid, List<String> params, Universe universe, NodeDetails node) {
    List<String> resolved = new ArrayList<>(params == null ? List.of() : params);
    String version = universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    if (version != null) {
      resolved.add("--yb_software_version");
      resolved.add(version);
    }
    resolved.add("--sensitive_gflags_b64");
    resolved.add(sensitiveGflagsB64(bundleUuid, version));
    resolved.add("--redact_bin");
    resolved.add(remoteBinaryPath(node, universe));
    return resolved;
  }

  /** Params telling the script to drop every staged server.conf, having no helper to redact it. */
  public List<String> appendDropServerConfScriptParam(List<String> params) {
    List<String> resolved = new ArrayList<>(params == null ? List.of() : params);
    resolved.add(DROP_SERVER_CONF_PARAM);
    return resolved;
  }

  private String sensitiveGflagsB64(UUID bundleUuid, String ybSoftwareVersion) {
    if (bundleUuid == null) {
      return encodeSensitiveGflagsB64(ybSoftwareVersion);
    }
    return sensitiveGflagsB64ByBundle
        .computeIfAbsent(bundleUuid, id -> new ConcurrentHashMap<>())
        .computeIfAbsent(
            ybSoftwareVersion == null ? "" : ybSoftwareVersion,
            v -> encodeSensitiveGflagsB64(ybSoftwareVersion));
  }

  /**
   * Base64 because the list travels as a single shell argument and gflag names are not guaranteed
   * shell-safe; encoding removes the quoting question rather than answering it per name.
   *
   * <p>{@code ysql_hba_conf_csv} is deliberately dropped from the wholesale list. Its value is a
   * working pg_hba ACL, which is exactly what support needs to debug an authentication problem, and
   * blanking the whole flag would destroy that. The Go redactor handles it separately and rewrites
   * only the inner {@code ldapbindpasswd=}; the {@code ysql_hba_conf_csv_excluded_from_wholesale}
   * golden case pins that behaviour.
   */
  private String encodeSensitiveGflagsB64(String ybSoftwareVersion) {
    Set<String> sensitiveGflags =
        RedactingService.getSensitiveGflagsForRedaction(ybSoftwareVersion, gFlagsValidation);
    List<String> gflagNames = new ArrayList<>(sensitiveGflags);
    gflagNames.removeIf(name -> GFlagsUtil.YSQL_HBA_CONF_CSV.equals(name));
    String json = Json.toJson(gflagNames).toString();
    return Base64.getEncoder().encodeToString(json.getBytes(StandardCharsets.UTF_8));
  }

  static String archSpecificBinaryName(String goArch) {
    return SupportBundleNodeBinarySupport.archSpecificBinaryName(REDACT_BINARY_NAME, goArch);
  }

  Optional<Path> resolveLocalBinaryPath(Optional<String> goArch) {
    return binarySupport.resolveLocalBinaryPath(REDACT_BINARY_NAME, goArch);
  }
}
