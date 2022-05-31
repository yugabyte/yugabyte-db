// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.CALLS_REAL_METHODS;
import static org.mockito.Mockito.mock;

import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.models.helpers.NodeDetails;

import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import junitparams.JUnitParamsRunner;
import junitparams.Parameters;

@RunWith(JUnitParamsRunner.class)
public class KubernetesTaskBaseTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @InjectMocks
  KubernetesTaskBase kubernetesTaskBase = mock(KubernetesTaskBase.class, CALLS_REAL_METHODS);

  @Test
  @Parameters({
    "yb-master-1, master, false",
    "yb-tserver-1_az-1, tserver, true",
  })
  public void testGetK8sNodeName(String podName, String server, boolean isMultiAz) {
    ServerType serverType = server.equals("master") ? ServerType.MASTER : ServerType.TSERVER;
    NodeDetails node =
        kubernetesTaskBase.getKubernetesNodeName(1, "az-1", serverType, isMultiAz, false);
    assertEquals(podName, node.nodeName);
  }

  @Test
  @Parameters({
    "yb-master-1, master, false, false",
    "demo-universe-yb-tserver-1, tserver, false, true",
    "yb-master-1, master, true, false",
    "demo-universe-az-1-yb-tserver-1, tserver, true, true"
  })
  public void testGetPodName(
      String podName, String server, boolean isMultiAz, boolean newNamingStyle) {
    ServerType serverType = server.equals("master") ? ServerType.MASTER : ServerType.TSERVER;
    String pod =
        kubernetesTaskBase.getPodName(
            1, "az-1", serverType, "demo-universe", isMultiAz, newNamingStyle);
    assertEquals(podName, pod);
  }
}
