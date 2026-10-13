// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.List;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Detects the cloud that an on-prem node physically runs on by probing the node's instance metadata
 * service.
 *
 * <p>An on-prem provider reports {@code cloudInfo.cloud = "onprem"} for every node, which says
 * nothing about the underlying infrastructure. Cross-cloud federated IAM does need to know it: an
 * on-prem universe can mix AWS and GCP VMs, and a node only needs federation for the cloud it is
 * <i>not</i> running on (an AWS node reaching GCS, a GCP node reaching S3).
 *
 * <p>Detection separates "this node is not a cloud VM" from "we could not find out", because the
 * two call for different handling: the first is a final answer, the second is worth retrying later.
 */
@Slf4j
@Singleton
public class NodeCloudDetector {

  // The metadata endpoints, headers and timeout mirror the ones ybops already uses - see
  // devops/opscli/ybops/cloud/gcp/utils.py (GcpMetadata) and
  // devops/opscli/ybops/cloud/aws/cloud.py (get_and_append_imdsv2_token_header).
  private static final String PROBE_TIMEOUT_SECS = "2";
  private static final String METADATA_IP = "169.254.169.254";
  private static final String GCP_METADATA_PATH = "/computeMetadata/v1/instance/id";
  private static final String AWS_INSTANCE_ID_URL =
      "http://" + METADATA_IP + "/latest/meta-data/instance-id";
  private static final String AWS_IMDSV2_TOKEN_URL = "http://" + METADATA_IP + "/latest/api/token";

  // GCE resolves metadata.google.internal; the link-local IP is the fallback for a node that
  // cannot resolve it.
  private static final List<String> GCP_PROBE_URLS =
      ImmutableList.of(
          "http://metadata.google.internal" + GCP_METADATA_PATH,
          "http://" + METADATA_IP + GCP_METADATA_PATH);

  // The metadata services are link-local and must be reached directly. A node may well have a
  // proxy configured (YBA has ProxyConfig, and YNP exports NO_PROXY into the shell), so tell curl
  // explicitly rather than relying on the node's environment listing these hosts. An explicit list
  // is used instead of "*" because the bash wrapper would glob-expand that.
  private static final String NO_PROXY_HOSTS = METADATA_IP + ",metadata.google.internal";

  // Shell exit code for a command that is not installed. curl is missing rather than the endpoint
  // being absent, so the probe told us nothing about the node's cloud.
  private static final int COMMAND_NOT_FOUND = 127;

  private final NodeUniverseManager nodeUniverseManager;

  @Inject
  public NodeCloudDetector(NodeUniverseManager nodeUniverseManager) {
    this.nodeUniverseManager = nodeUniverseManager;
  }

  /**
   * Returns the cloud the node physically runs on:
   *
   * <ul>
   *   <li>{@link CloudType#aws} or {@link CloudType#gcp} - that metadata service answered.
   *   <li>{@link CloudType#onprem} - every probe ran and none answered, so the node is not an AWS
   *       or GCP VM.
   *   <li>{@code null} - at least one probe could not be run (the node was unreachable, or {@code
   *       curl} is missing), so nothing was learned.
   * </ul>
   *
   * <p>GCP is checked first because GCE requires the {@code Metadata-Flavor} header, so an answer
   * there identifies GCP unambiguously; AWS IMDS replies 404 to the GCP path and vice versa, so
   * neither probe can produce a false positive on the other cloud.
   */
  public CloudType detect(Universe universe, NodeDetails node, Duration probeTimeout) {
    ShellProcessContext context =
        ShellProcessContext.builder()
            .logCmdOutput(true)
            .timeoutSecs(probeTimeout.getSeconds())
            .build();
    boolean allProbesRan = true;

    for (String url : GCP_PROBE_URLS) {
      ShellResponse response =
          runProbe(universe, node, context, curl("-H", "Metadata-Flavor:Google", url));
      if (response == null) {
        allProbesRan = false;
      } else if (response.isSuccess()) {
        return CloudType.gcp;
      }
    }

    // IMDSv2 requires a token first; fall back to IMDSv1 for instances that still allow it.
    ShellResponse tokenResponse =
        runProbe(
            universe,
            node,
            context,
            curl(
                "-X",
                "PUT",
                AWS_IMDSV2_TOKEN_URL,
                "-H",
                "X-aws-ec2-metadata-token-ttl-seconds:60"));
    if (tokenResponse == null) {
      allProbesRan = false;
    } else {
      String token = extractImdsV2Token(tokenResponse);
      if (token != null) {
        ShellResponse response =
            runProbe(
                universe,
                node,
                context,
                curl("-H", "X-aws-ec2-metadata-token:" + token, AWS_INSTANCE_ID_URL));
        if (response == null) {
          allProbesRan = false;
        } else if (response.isSuccess()) {
          return CloudType.aws;
        }
      }
    }

    ShellResponse imdsV1Response = runProbe(universe, node, context, curl(AWS_INSTANCE_ID_URL));
    if (imdsV1Response == null) {
      allProbesRan = false;
    } else if (imdsV1Response.isSuccess()) {
      return CloudType.aws;
    }

    if (!allProbesRan) {
      log.warn("Could not probe the node's metadata services; its cloud stays unknown");
      return null;
    }
    log.info("Node answered neither metadata service; recording it as a physical node");
    return CloudType.onprem;
  }

  /** Returns the IMDSv2 token, or null when the endpoint refused or returned something unusable. */
  private static String extractImdsV2Token(ShellResponse response) {
    if (!response.isSuccess() || StringUtils.isBlank(response.message)) {
      return null;
    }
    String token = response.message.trim();
    // The token is opaque, so refuse anything that would not survive being passed back as a single
    // shell argument instead of risking a malformed command.
    if (StringUtils.containsWhitespace(token)) {
      log.warn("Ignoring malformed IMDSv2 token from the node");
      return null;
    }
    return token;
  }

  /**
   * Builds a curl invocation. Every token is free of spaces and quotes on purpose: the node-agent
   * bash wrapper single-quotes any argument that contains a space without escaping it (see {@code
   * NodeAgentClient.getBashCommand}), so a token carrying its own quoting would produce a malformed
   * command. curl accepts {@code -H Header:Value} without a space, which is what lets us avoid it.
   */
  private static List<String> curl(String... args) {
    return ImmutableList.<String>builder()
        .add("curl", "-sf", "-m", PROBE_TIMEOUT_SECS, "--noproxy", NO_PROXY_HOSTS)
        .add(args)
        .build();
  }

  /**
   * Runs one probe. Returns the response when the probe executed - its exit code then says whether
   * the endpoint answered - or null when the probe could not be run at all.
   */
  private ShellResponse runProbe(
      Universe universe, NodeDetails node, ShellProcessContext context, List<String> command) {
    ShellResponse response;
    try {
      response = nodeUniverseManager.runCommand(node, universe, command, context);
    } catch (Exception e) {
      log.warn("Cloud detection probe {} could not be run: {}", command, e.getMessage());
      return null;
    }
    if (response == null || response.code == COMMAND_NOT_FOUND) {
      log.warn("Cloud detection probe {} could not be run: curl is missing on the node", command);
      return null;
    }
    if (!response.isSuccess()) {
      log.debug("Cloud detection probe {} exited {}: {}", command, response.code, response.message);
    }
    return response;
  }
}
