// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.CALLS_REAL_METHODS;
import static org.mockito.Mockito.mock;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

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
    "yb-master-1, master, false, false, false",
    "ybdemo-universe-vyss-yb-tserver-1, tserver, false, true, false",
    "ybdemo-univer-rr-edve-yb-tserver-1, tserver, false, true, true",
    "yb-master-1, master, true, false, false",
    "ybdemo-univer-az-1-vjoo-yb-tserver-1, tserver, true, true, false",
    "ybdemo-univer-az-1rr-iciu-yb-tserver-1, tserver, true, true, true"
  })
  public void testGetPodName(
      String podName,
      String server,
      boolean isMultiAz,
      boolean newNamingStyle,
      boolean isReadOnlyCluster) {
    ServerType serverType = server.equals("master") ? ServerType.MASTER : ServerType.TSERVER;
    String pod =
        kubernetesTaskBase.getPodName(
            1,
            "az-1",
            serverType,
            "demo-universe",
            isMultiAz,
            newNamingStyle,
            "demo-universe",
            isReadOnlyCluster);
    assertEquals(podName, pod);
  }
}
