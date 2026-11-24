// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2.manifest;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.dataformat.yaml.YAMLFactory;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentLevel;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.EnumUtils;
import org.apache.commons.lang3.StringUtils;

/**
 * Reads the support bundle v2 manifest that ships in the devops directory, next to the {@code
 * support_bundle_utils.sh} and {@code yba_utils.sh} scripts it references.
 *
 * <p>Every {@link #load()} hands back a private deep copy. Callers overlay request supplied values
 * onto the component params before building specs, so a shared instance would leak one request's
 * overrides into the next.
 */
@Slf4j
@Singleton
public class SupportBundleV2ManifestLoader {

  /** Relative to {@code yb.devops.home}, the same root script paths are resolved against. */
  public static final String MANIFEST_RELATIVE_PATH = "bin/support_bundlev2_manifest.yaml";

  /** The group under {@code GlobalComponents} whose members run as scripts on the YBA host. */
  public static final String YBA_GLOBAL_GROUP = "YBA";

  /** The groups under {@code NodeComponents} whose members run as scripts on DB nodes. */
  public static final String FILES_NODE_GROUP = "Files";

  public static final String BASH_NODE_GROUP = "Bash";

  private final Config staticConf;
  private final ObjectMapper yamlMapper;
  private final ObjectMapper copyMapper;

  private Path cachedPath;
  private long cachedLastModified;
  private long cachedSize;
  private SupportBundleV2Manifest cached;

  /**
   * Both mappers derive from YBA's shared one. It is JSON-only, so the parser is a copy with a YAML
   * factory. The deep copy keeps nulls: the shared mapper writes NON_NULL, which would turn an
   * explicit null in the manifest into the field's default.
   */
  @Inject
  public SupportBundleV2ManifestLoader(Config staticConf, ObjectMapper mapper) {
    this.staticConf = staticConf;
    this.yamlMapper = mapper.copyWith(new YAMLFactory());
    this.copyMapper = mapper.copy().setSerializationInclusion(JsonInclude.Include.ALWAYS);
  }

  public Path manifestPath() {
    return Paths.get(staticConf.getString("yb.devops.home"), MANIFEST_RELATIVE_PATH);
  }

  /** Returns a deep copy of the parsed manifest, reparsing only when the file changed on disk. */
  public synchronized SupportBundleV2Manifest load() {
    Path path = manifestPath();
    if (!Files.isRegularFile(path)) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format(
              "Support bundle manifest %s is missing; check the YBA installation (yb.devops.home)",
              path));
    }
    long lastModified;
    long size;
    try {
      lastModified = Files.getLastModifiedTime(path).toMillis();
      size = Files.size(path);
    } catch (IOException e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format("Cannot stat support bundle manifest %s: %s", path, e.getMessage()));
    }

    if (cached == null
        || !path.equals(cachedPath)
        || lastModified != cachedLastModified
        || size != cachedSize) {
      cached = parse(path);
      cachedPath = path;
      cachedLastModified = lastModified;
      cachedSize = size;
      log.info("Loaded support bundle v2 manifest from {}", path);
    }
    return copyMapper.convertValue(cached, SupportBundleV2Manifest.class);
  }

  private SupportBundleV2Manifest parse(Path path) {
    SupportBundleV2Manifest manifest;
    try {
      manifest = yamlMapper.readValue(path.toFile(), SupportBundleV2Manifest.class);
    } catch (IOException e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format("Cannot parse support bundle manifest %s: %s", path, e.getMessage()));
    }
    if (manifest == null) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, String.format("Support bundle manifest %s is empty", path));
    }
    normalizeGlobalComponents(manifest);
    return manifest;
  }

  /**
   * Whether {@code name} is collected by its v1 component: a global-level type that the manifest
   * doesn't configure as a script. Derived, so listing an existing v1 global needs no code change.
   */
  public static boolean isV1GlobalComponent(SupportBundleV2Manifest.Spec spec, String name) {
    ComponentType type = EnumUtils.getEnum(ComponentType.class, name);
    // YBAComponent is v2's runner for YBA-host scripts, not a collector of its own.
    if (type == null
        || type.getComponentLevel() != ComponentLevel.GlobalLevel
        || type == ComponentType.YBAComponent) {
      return false;
    }
    SupportBundleV2Manifest.Component config = spec.getComponentConfigs().get(name);
    return config == null
        || config.getScript() == null
        || StringUtils.isBlank(config.getScript().getEntrypoint());
  }

  /**
   * Expands the shorthand a legacy global uses to name itself: {@code YbaMetadata:} with no list
   * under it means the single component {@code YbaMetadata}. The {@code YBA} group is left alone,
   * since its members are script components that never share a name with their group.
   */
  static void normalizeGlobalComponents(SupportBundleV2Manifest manifest) {
    Map<String, List<String>> globals = manifest.getSpec().getGlobalComponents();
    for (Map.Entry<String, List<String>> entry : globals.entrySet()) {
      if (YBA_GLOBAL_GROUP.equals(entry.getKey())) {
        continue;
      }
      List<String> members = entry.getValue();
      if (members == null || members.isEmpty()) {
        entry.setValue(new ArrayList<>(Collections.singletonList(entry.getKey())));
      }
    }
  }
}
