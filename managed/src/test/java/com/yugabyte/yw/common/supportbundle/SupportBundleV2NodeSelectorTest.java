// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.supportbundle.SupportBundleV2NodeSelector.NodeSelectionResult;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Set;
import org.junit.Before;
import org.junit.Test;
import play.mvc.Http.Status;

public class SupportBundleV2NodeSelectorTest {

  private static final List<String> NODE_NAMES = Arrays.asList("n1", "n2", "n3");

  private Universe universe;
  private SupportBundleV2NodeSelector nodeSelector;

  @Before
  public void setUp() {
    universe = mock(Universe.class);
    when(universe.getNodes()).thenReturn(Arrays.asList(node("n1"), node("n2"), node("n3")));
    nodeSelector = new SupportBundleV2NodeSelector();
  }

  private static NodeDetails node(String nodeName) {
    NodeDetails node = new NodeDetails();
    node.nodeName = nodeName;
    return node;
  }

  @Test
  public void testNullSelectionReturnsAllNodes() {
    NodeSelectionResult result = nodeSelector.resolve(universe, null);
    assertEquals(NODE_NAMES, SupportBundleV2NodeSelector.nodeNamesOf(result.getSelectedNodes()));
    assertTrue(result.getUnmatchedNodeNames().isEmpty());
  }

  @Test
  public void testEmptySelectionReturnsAllNodes() {
    NodeSelectionResult result = nodeSelector.resolve(universe, Collections.emptyList());
    assertEquals(NODE_NAMES, SupportBundleV2NodeSelector.nodeNamesOf(result.getSelectedNodes()));
    assertTrue(result.getUnmatchedNodeNames().isEmpty());
  }

  @Test
  public void testSingleMatchingNodeIsSelected() {
    Set<NodeDetails> selected =
        nodeSelector.resolveOrThrow(universe, Collections.singletonList("n2"));
    assertEquals(
        Collections.singletonList("n2"), SupportBundleV2NodeSelector.nodeNamesOf(selected));
  }

  @Test
  public void testSingleUnknownNodeThrowsBadRequest() {
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> nodeSelector.resolveOrThrow(universe, Collections.singletonList("no-such-node")));
    assertEquals(Status.BAD_REQUEST, exception.getHttpStatus());
    assertTrue(exception.getMessage().contains("no-such-node"));
  }

  @Test
  public void testAllUnknownNodesThrowBadRequest() {
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> nodeSelector.resolveOrThrow(universe, Arrays.asList("nope-1", "nope-2")));
    assertEquals(Status.BAD_REQUEST, exception.getHttpStatus());
  }

  @Test
  public void testPartialMatchSelectsKnownNodesAndReportsMisses() {
    NodeSelectionResult result = nodeSelector.resolve(universe, Arrays.asList("n1", "nope-1"));
    assertEquals(
        Collections.singletonList("n1"),
        SupportBundleV2NodeSelector.nodeNamesOf(result.getSelectedNodes()));
    assertEquals(Collections.singletonList("nope-1"), result.getUnmatchedNodeNames());
  }

  @Test
  public void testPartialMatchDoesNotThrow() {
    Set<NodeDetails> selected = nodeSelector.resolveOrThrow(universe, Arrays.asList("n1", "nope"));
    assertEquals(
        Collections.singletonList("n1"), SupportBundleV2NodeSelector.nodeNamesOf(selected));
  }

  @Test
  public void testDuplicatesAndBlanksAreNormalized() {
    NodeSelectionResult result =
        nodeSelector.resolve(universe, Arrays.asList("n1", "  n1  ", "", "   ", null));
    assertEquals(
        Collections.singletonList("n1"),
        SupportBundleV2NodeSelector.nodeNamesOf(result.getSelectedNodes()));
    assertTrue(result.getUnmatchedNodeNames().isEmpty());
  }

  @Test
  public void testSelectionPreservesRequestedOrdering() {
    List<String> requested = Arrays.asList("n3", "n1");
    NodeSelectionResult result = nodeSelector.resolve(universe, requested);
    assertEquals(requested, SupportBundleV2NodeSelector.nodeNamesOf(result.getSelectedNodes()));
  }
}
