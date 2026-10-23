// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;

public class NodeCloudDetectorTest {

  @Mock private NodeUniverseManager mockNodeUniverseManager;
  @Mock private Universe mockUniverse;

  private NodeCloudDetector nodeCloudDetector;

  @Before
  public void setup() {
    MockitoAnnotations.openMocks(this);
    nodeCloudDetector = new NodeCloudDetector(mockNodeUniverseManager);
  }

  /** Runs detection with the node's metadata services answering as {@code fakeNode} describes. */
  private CloudType detect(FakeNode fakeNode) {
    when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenAnswer(invocation -> fakeNode.run(invocation.getArgument(2)));
    return nodeCloudDetector.detect(mockUniverse, new NodeDetails(), Duration.ofSeconds(30));
  }

  // curl's exit code when -f sees an HTTP error, i.e. the endpoint refused the request.
  private static final int CURL_HTTP_ERROR = 22;
  // curl's exit code when the host could not be resolved.
  private static final int CURL_UNRESOLVED_HOST = 6;
  // Shell exit code when curl itself is not installed.
  private static final int COMMAND_NOT_FOUND = 127;

  /** Answers each probe based on the URL it targets, recording what was run. */
  private static class FakeNode {
    final List<List<String>> commands = new ArrayList<>();
    String gcpInstanceId;
    String imdsV2Token;
    String awsInstanceId;
    boolean resolveGcpDnsName = true;
    // Set for an instance whose IMDS is configured to require a v2 token.
    boolean requireImdsV2;
    // Set for a node that does not have curl installed.
    boolean curlMissing;
    RuntimeException failure;

    ShellResponse run(List<String> command) {
      commands.add(command);
      if (failure != null) {
        throw failure;
      }
      if (curlMissing) {
        return ShellResponse.create(COMMAND_NOT_FOUND, "curl: command not found");
      }
      // Locate the target by scheme, not by position: curl takes its options in any order, so the
      // URL is not necessarily the last argument.
      String url = command.stream().filter(a -> a.startsWith("http://")).findFirst().orElse("");
      if (url.contains("/computeMetadata/")) {
        if (url.contains("metadata.google.internal") && !resolveGcpDnsName) {
          return ShellResponse.create(CURL_UNRESOLVED_HOST, "");
        }
        return respond(gcpInstanceId);
      }
      if (url.contains("/latest/api/token")) {
        return respond(imdsV2Token);
      }
      boolean sentToken = command.stream().anyMatch(a -> a.startsWith("X-aws-ec2-metadata-token:"));
      if (requireImdsV2 && !sentToken) {
        return ShellResponse.create(CURL_HTTP_ERROR, "");
      }
      return respond(awsInstanceId);
    }

    private ShellResponse respond(String body) {
      return body == null
          ? ShellResponse.create(CURL_HTTP_ERROR, "")
          : ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, body);
    }
  }

  @Test
  public void testDetectGcp() {
    FakeNode node = new FakeNode();
    node.gcpInstanceId = "4philo2932481";
    assertEquals(CloudType.gcp, detect(node));
    // GCP is probed first, so nothing should have touched AWS IMDS.
    assertEquals(1, node.commands.size());
    assertTrue(node.commands.get(0).contains("Metadata-Flavor:Google"));
  }

  @Test
  public void testDetectGcpWhenDnsNameDoesNotResolve() {
    FakeNode node = new FakeNode();
    node.gcpInstanceId = "4philo2932481";
    node.resolveGcpDnsName = false;
    assertEquals(CloudType.gcp, detect(node));
    // Falls back to the link-local IP.
    assertEquals(2, node.commands.size());
  }

  @Test
  public void testDetectAwsWithImdsV2() {
    FakeNode node = new FakeNode();
    node.imdsV2Token = "AQAEALtq_token_value";
    node.awsInstanceId = "i-0abc123";
    node.requireImdsV2 = true;
    assertEquals(CloudType.aws, detect(node));
    assertTrue(
        node.commands.stream()
            .anyMatch(c -> c.contains("X-aws-ec2-metadata-token:AQAEALtq_token_value")));
  }

  @Test
  public void testDetectAwsWithImdsV1() {
    FakeNode node = new FakeNode();
    // No token endpoint, so IMDSv2 is unavailable and the v1 fallback answers.
    node.awsInstanceId = "i-0abc123";
    assertEquals(CloudType.aws, detect(node));
  }

  @Test
  public void testGcpIsPreferredOverAws() {
    // A GCE instance still has something listening on the link-local IP, so the order matters.
    FakeNode node = new FakeNode();
    node.gcpInstanceId = "4philo2932481";
    node.awsInstanceId = "i-0abc123";
    assertEquals(CloudType.gcp, detect(node));
  }

  @Test
  public void testBareMetalReportsOnprem() {
    // Every probe ran and none answered, so this is a final answer: not a cloud VM.
    assertEquals(CloudType.onprem, detect(new FakeNode()));
  }

  @Test
  public void testUnreachableNodeIsUnknown() {
    FakeNode node = new FakeNode();
    node.failure = new RuntimeException("node unreachable");
    // Nothing was learned, which must not be confused with bare metal.
    assertNull(detect(node));
  }

  @Test
  public void testMissingCurlIsUnknown() {
    FakeNode node = new FakeNode();
    node.curlMissing = true;
    // The probe could not run, so we know nothing about this node's cloud.
    assertNull(detect(node));
  }

  @Test
  public void testImdsV2TokenWithWhitespaceIsRejected() {
    FakeNode node = new FakeNode();
    node.imdsV2Token = "bad token";
    node.awsInstanceId = "i-0abc123";
    // The token is unusable, so no probe may carry it; the v1 fallback still identifies AWS.
    assertEquals(CloudType.aws, detect(node));
    assertTrue(
        node.commands.stream()
            .flatMap(List::stream)
            .noneMatch(a -> a.startsWith("X-aws-ec2-metadata-token:")));
  }

  @Test
  public void testEveryProbeBypassesTheProxy() {
    FakeNode node = new FakeNode();
    node.imdsV2Token = "AQAEALtq_token_value";
    node.awsInstanceId = "i-0abc123";
    node.requireImdsV2 = true;
    detect(node);
    // The metadata services are link-local, so a proxy configured on the node must never be used
    // for them - otherwise detection silently answers for the proxy instead of the node.
    for (List<String> command : node.commands) {
      int at = command.indexOf("--noproxy");
      assertTrue("probe must bypass the proxy: " + command, at >= 0);
      assertTrue(command.get(at + 1).contains("169.254.169.254"));
      assertTrue(command.get(at + 1).contains("metadata.google.internal"));
    }
  }

  @Test
  public void testProbeArgumentsAreShellSafe() {
    FakeNode node = new FakeNode();
    node.imdsV2Token = "AQAEALtq_token_value";
    node.awsInstanceId = "i-0abc123";
    node.requireImdsV2 = true;
    detect(node);
    // The node-agent bash wrapper single-quotes any argument containing a space without escaping
    // it, so no probe argument may contain a space or a quote.
    for (List<String> command : node.commands) {
      for (String arg : command) {
        assertTrue(
            "argument must be shell-safe: " + arg,
            !arg.contains(" ") && !arg.contains("'") && !arg.contains("\"") && !arg.contains("*"));
      }
    }
  }
}
