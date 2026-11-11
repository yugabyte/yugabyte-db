// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2.manifest;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import lombok.Data;

/**
 * In-memory form of {@code devops/bin/support_bundlev2_manifest.yaml}.
 *
 * <p>Field for field mirror of the structs the yba-cli unmarshals in {@code
 * yba-cli/cmd/universe/supportbundlev2/supportbundlev2.go}. The two readers have to agree: the CLI
 * turns this file into a create request client side, and {@link SupportBundleV2ManifestMerger}
 * turns it into the same request server side for callers that send component names without specs. A
 * field added on one side but not the other silently changes what one of those two paths collects.
 *
 * <p>The yaml's {@code kind}, {@code metadata} and {@code spec.output} keys are not bound here.
 * {@code output} describes a post-execution stdout contract that only the CLI consumes.
 */
@Data
@JsonIgnoreProperties(ignoreUnknown = true)
public class SupportBundleV2Manifest {

  private Spec spec = new Spec();

  @Data
  @JsonIgnoreProperties(ignoreUnknown = true)
  public static class Spec {

    private CollectionTimeRange collectionTimeRange = new CollectionTimeRange();

    private NodeSelection nodeSelection = new NodeSelection();

    @JsonProperty("GlobalComponents")
    private Map<String, List<String>> globalComponents = new LinkedHashMap<>();

    @JsonProperty("NodeComponents")
    private Map<String, List<String>> nodeComponents = new LinkedHashMap<>();

    private Execution execution = new Execution();

    private Defaults defaults = new Defaults();

    private Map<String, Component> componentConfigs = new LinkedHashMap<>();

    /**
     * Stamped once per request rather than read from the yaml, so every {@code
     * ${currentTimestampUTC}} in a single bundle resolves to the same instant.
     */
    @JsonIgnore private String requestBuildTimeUtc;
  }

  @Data
  @JsonIgnoreProperties(ignoreUnknown = true)
  public static class CollectionTimeRange {
    private String startDate;
    private String endDate;
  }

  @Data
  @JsonIgnoreProperties(ignoreUnknown = true)
  public static class NodeSelection {
    private List<String> nodeNames = new ArrayList<>();
  }

  @Data
  @JsonIgnoreProperties(ignoreUnknown = true)
  public static class Execution {
    private String linuxUser;
    private long timeoutSecs;
  }

  @Data
  @JsonIgnoreProperties(ignoreUnknown = true)
  public static class Defaults {
    private String mountPath;
    private String nodeDataDirTemplate;
    private String ybHomeDir;
    private String remoteTmpDir;
    private String nodeAgentInstallPath;
    private boolean filterPgAuditLogs;
  }

  @Data
  @JsonIgnoreProperties(ignoreUnknown = true)
  public static class Component {
    private boolean enabled;
    private RunAs runAs = RunAs.YUGABYTE;
    // YBA deploys the redaction helper and the node redacts any server.conf the component stages.
    private boolean redactServerConf;
    private Scope scope = Scope.NODE;
    private Script script = new Script();
    private Map<String, Object> params = new LinkedHashMap<>();
  }

  /** The ssh user a node script runs as; set only in YBA's manifest. */
  public enum RunAs {
    @JsonProperty("yugabyte")
    YUGABYTE,
    /** Normally has passwordless sudo. */
    @JsonProperty("providerSshUser")
    PROVIDER_SSH_USER
  }

  /** Where a YSQL, YCQL or yb-admin member runs; set only in YBA's manifest. */
  public enum Scope {
    @JsonProperty("node")
    NODE,
    /** Reads cluster-wide state, so it runs on one node per bundle. */
    @JsonProperty("cluster")
    CLUSTER
  }

  @Data
  @JsonIgnoreProperties(ignoreUnknown = true)
  public static class Script {
    private String scriptFile;
    private String entrypoint;
  }
}
