// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.checkTagPattern;
import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.getNodeName;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.core.StringContains.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

@RunWith(JUnitParamsRunner.class)
public class UniverseDefinitionTaskBaseTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private Cluster myCluster;
  private NodeDetails myNode;
  private UserIntent userIntent;

  @Mock private BaseTaskDependencies baseTaskDependencies;

  @Before
  public void setUp() {
    myNode = ApiUtils.getDummyNodeDetails(1, NodeState.Live);
    myNode.nodeName = null;
    assertEquals("test-region", myNode.cloudInfo.region);
    assertEquals("az-1", myNode.cloudInfo.az);
    userIntent = new UserIntent();
    userIntent.numNodes = 1;
    userIntent.replicationFactor = 1;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.universeName = "TagsTestUniverse";
    myCluster = new Cluster(ClusterType.PRIMARY, userIntent);
  }

  @Test
  public void testTagPattern() {
    String mytag = "${universe}-test-${region}-${instance-id}";
    checkTagPattern(mytag);

    mytag = "${universe}-${region}-${instance-id}";
    checkTagPattern(mytag);

    mytag = "${instance-id}";
    checkTagPattern(mytag);

    mytag = "${instance-id}-${universe}";
    checkTagPattern(mytag);

    mytag = "${universe}-${instance-id}.aws.acme.io";
    checkTagPattern(mytag);

    mytag = "${universe}-${instance-id}.${region}-${zone}";
    checkTagPattern(mytag);

    mytag = "${universe}.aws.test-${instance-id}.${region}-${zone}";
    checkTagPattern(mytag);

    mytag = "yb-data-${instance-id}.${region}.gcp.acme.io";
    checkTagPattern(mytag);
  }

  private String getTestNodeName(String tag) {
    return getNodeName(
        myCluster, tag, "oldPrefix", myNode.nodeIdx, myNode.cloudInfo.region, myNode.cloudInfo.az);
  }

  @Test
  public void testNodeNameFromTags() {
    String tag = "";
    assertEquals("oldPrefix-n1", getTestNodeName(tag));

    tag = "${universe}-${instance-id}-${region}";
    assertEquals("TagsTestUniverse-1-test-region", getTestNodeName(tag));

    tag = "${universe}-test-${instance-id}-${region}";
    assertEquals("TagsTestUniverse-test-1-test-region", getTestNodeName(tag));

    tag = "${universe}-test-${instance-id}-${region}-${zone}";
    assertEquals("TagsTestUniverse-test-1-test-region-az-1", getTestNodeName(tag));

    tag = "${instance-id}.${region}-${zone}-${universe}";
    assertEquals("1.test-region-az-1-TagsTestUniverse", getTestNodeName(tag));

    tag = "${universe}-test-${instance-id}.${region}-${zone}";
    assertEquals("TagsTestUniverse-test-1.test-region-az-1", getTestNodeName(tag));

    tag = "${universe}-test-${instance-id}.${region}-${zone}.aws.acme.io";
    assertEquals("TagsTestUniverse-test-1.test-region-az-1.aws.acme.io", getTestNodeName(tag));

    tag = "yb-data-${instance-id}.${region}.azure.acme.io";
    assertEquals("yb-data-1.test-region.azure.acme.io", getTestNodeName(tag));

    tag = "${instance-id}.${region}.azure.acme.io";
    assertEquals("1.test-region.azure.acme.io", getTestNodeName(tag));

    tag = "${instance-id}";
    assertEquals("1", getTestNodeName(tag));

    tag = "my-ABCuniv!-${instance-id}";
    assertEquals("my-ABCuniv!-1", getTestNodeName(tag));

    Cluster tempCluster = new Cluster(ClusterType.ASYNC, userIntent);
    String name =
        getNodeName(
            tempCluster,
            "",
            "oldPrefix",
            myNode.nodeIdx,
            myNode.cloudInfo.region,
            myNode.cloudInfo.az);
    assertEquals("oldPrefix-readonly0-n1", name);

    name =
        getNodeName(
            tempCluster,
            "${universe}-${instance-id}",
            "oldPrefix",
            myNode.nodeIdx,
            myNode.cloudInfo.region,
            myNode.cloudInfo.az);
    assertEquals("TagsTestUniverse-1-readonly0", name);
  }

  @Test
  public void testNameTagsFailures() {
    try {
      checkTagPattern("");
      fail();
    } catch (RuntimeException e) {
      assertThat(
          e.getMessage(), allOf(notNullValue(), containsString("Invalid value '' for Name")));
    }

    try {
      checkTagPattern("${universe}-${zone");
      fail();
    } catch (RuntimeException e) {
      assertThat(
          e.getMessage(),
          allOf(notNullValue(), containsString("Number of '${' does not match '}'")));
    }

    try {
      checkTagPattern("universe}-${zone}-${region}");
      fail();
    } catch (RuntimeException e) {
      assertThat(
          e.getMessage(),
          allOf(notNullValue(), containsString("Number of '${' does not match '}'")));
    }

    try {
      checkTagPattern("${universe-${zone}}-${region}");
      fail();
    } catch (RuntimeException e) {
      assertThat(
          e.getMessage(), allOf(notNullValue(), containsString("Invalid variable universe-")));
    }

    try {
      checkTagPattern("${wrongkey}-${region}");
      fail();
    } catch (RuntimeException e) {
      assertThat(
          e.getMessage(), allOf(notNullValue(), containsString("Invalid variable wrongkey")));
    }

    try {
      checkTagPattern("${universe}.${region}-${zone}");
      fail();
    } catch (RuntimeException e) {
      assertThat(
          e.getMessage(), allOf(notNullValue(), containsString("should be part of Name value")));
    }

    try {
      checkTagPattern("${universe}-${universe}-test-${region}");
      fail();
    } catch (RuntimeException e) {
      assertThat(e.getMessage(), allOf(notNullValue(), containsString("Duplicate universe")));
    }
  }
}
