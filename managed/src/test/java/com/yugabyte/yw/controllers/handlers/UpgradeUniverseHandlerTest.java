// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Collections;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class UpgradeUniverseHandlerTest {
  private static final String DEFAULT_INSTANCE_TYPE = "type1";
  private static final String NEW_INSTANCE_TYPE = "type2";
  private UpgradeUniverseHandler handler;

  @Before
  public void setUp() {
    Commissioner mockCommissioner = mock(Commissioner.class);
    when(mockCommissioner.submit(any(TaskType.class), any(ITaskParams.class)))
        .thenReturn(UUID.randomUUID());
    handler =
        new UpgradeUniverseHandler(
            mockCommissioner,
            mock(KubernetesManagerFactory.class),
            mock(RuntimeConfigFactory.class));
  }

  private static Object[] tlsToggleCustomTypeNameParams() {
    return new Object[][] {
      {false, false, true, false, "TLS Toggle ON"},
      {false, false, false, true, "TLS Toggle ON"},
      {false, false, true, true, "TLS Toggle ON"},
      {true, false, true, true, "TLS Toggle ON"},
      {false, true, true, true, "TLS Toggle ON"},
      {true, true, false, true, "TLS Toggle OFF"},
      {true, true, true, false, "TLS Toggle OFF"},
      {true, true, false, false, "TLS Toggle OFF"},
      {false, true, false, false, "TLS Toggle OFF"},
      {true, false, false, false, "TLS Toggle OFF"},
      {false, true, true, false, "TLS Toggle Client ON Node OFF"},
      {true, false, false, true, "TLS Toggle Client OFF Node ON"}
    };
  }

  @Test
  @Parameters(method = "tlsToggleCustomTypeNameParams")
  public void testTLSToggleCustomTypeName(
      boolean clientBefore,
      boolean nodeBefore,
      boolean clientAfter,
      boolean nodeAfter,
      String expected) {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.enableClientToNodeEncrypt = clientBefore;
    userIntent.enableNodeToNodeEncrypt = nodeBefore;
    TlsToggleParams requestParams = new TlsToggleParams();
    requestParams.enableClientToNodeEncrypt = clientAfter;
    requestParams.enableNodeToNodeEncrypt = nodeAfter;

    String result = UpgradeUniverseHandler.generateTypeName(userIntent, requestParams);
    Assert.assertEquals(expected, result);
  }

  @Test
  public void testMergeResizeNodeParamsWithIntent() {
    UniverseDefinitionTaskParams.UserIntent intent = new UniverseDefinitionTaskParams.UserIntent();
    intent.providerType = Common.CloudType.aws;
    intent.deviceInfo = new DeviceInfo();
    intent.deviceInfo.volumeSize = 100;
    intent.deviceInfo.numVolumes = 2;
    intent.replicationFactor = 35;
    intent.instanceType = DEFAULT_INSTANCE_TYPE;
    UUID universeCA = UUID.randomUUID();
    Universe universe = new Universe();
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.clusters =
        Collections.singletonList(
            new UniverseDefinitionTaskParams.Cluster(
                UniverseDefinitionTaskParams.ClusterType.PRIMARY, intent));
    taskParams.nodeDetailsSet = Collections.emptySet();
    UUID clusterId = UUID.randomUUID();
    taskParams.clusters.get(0).uuid = clusterId;
    universe.setUniverseDetails(taskParams);
    universe.getUniverseDetails().rootCA = universeCA;
    universe.getUniverseDetails().clientRootCA = universeCA;

    ResizeNodeParams resizeNodeParams = new ResizeNodeParams();
    resizeNodeParams.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
    resizeNodeParams.rootCA = UUID.randomUUID();
    resizeNodeParams.clientRootCA = UUID.randomUUID();
    UniverseDefinitionTaskParams.UserIntent requestIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    requestIntent.deviceInfo = new DeviceInfo();
    requestIntent.deviceInfo.volumeSize = 150;
    requestIntent.instanceType = NEW_INSTANCE_TYPE;
    resizeNodeParams.clusters =
        Collections.singletonList(
            new UniverseDefinitionTaskParams.Cluster(
                UniverseDefinitionTaskParams.ClusterType.PRIMARY, requestIntent));
    resizeNodeParams.clusters.get(0).uuid = clusterId;

    handler.mergeResizeNodeParamsWithIntent(resizeNodeParams, universe);
    Assert.assertEquals(universeCA, resizeNodeParams.rootCA);
    Assert.assertEquals(universeCA, resizeNodeParams.clientRootCA);
    UniverseDefinitionTaskParams.UserIntent submitIntent =
        resizeNodeParams.getPrimaryCluster().userIntent;
    Assert.assertEquals(35, submitIntent.replicationFactor);
    Assert.assertEquals(NEW_INSTANCE_TYPE, submitIntent.instanceType);
    Assert.assertEquals(150, submitIntent.deviceInfo.volumeSize.intValue());
    Assert.assertEquals(2, submitIntent.deviceInfo.numVolumes.intValue());
  }
}
