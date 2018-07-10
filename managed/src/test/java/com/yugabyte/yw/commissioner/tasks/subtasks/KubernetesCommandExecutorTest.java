// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.KubernetesManager;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.test.Helpers;

import java.util.Map;

import static org.junit.Assert.assertEquals;
import static org.mockito.Matchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

public class KubernetesCommandExecutorTest extends SubTaskBaseTest {
  KubernetesManager kubernetesManager;
  Provider defaultProvider;
  Universe defaultUniverse;

  @Override
  protected Application provideApplication() {
    kubernetesManager = mock(KubernetesManager.class);
    return new GuiceApplicationBuilder()
        .configure((Map) Helpers.inMemoryDatabase())
        .overrides(bind(KubernetesManager.class).toInstance(kubernetesManager))
        .build();
  }

  @Before
  public void setUp() {
    super.setUp();
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    defaultUniverse = Universe.saveDetails(defaultUniverse.universeUUID,
        ApiUtils.mockUniverseUpdater(Common.CloudType.kubernetes));
  }

  private KubernetesCommandExecutor createExecutor(KubernetesCommandExecutor.CommandType commandType) {
    KubernetesCommandExecutor kubernetesCommandExecutor = new KubernetesCommandExecutor();
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    params.providerUUID = defaultProvider.uuid;
    params.commandType = commandType;
    params.nodePrefix = defaultUniverse.getUniverseDetails().nodePrefix;
    params.universeUUID = defaultUniverse.universeUUID;
    kubernetesCommandExecutor.initialize(params);
    return kubernetesCommandExecutor;
  }

  @Test
  public void testHelmInit() {
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(KubernetesCommandExecutor.CommandType.HELM_INIT);
    verify(kubernetesManager, times(0)).helmInit(defaultProvider.uuid);
    kubernetesCommandExecutor.run();
  }

  @Test
  public void testHelmInstall() {
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(KubernetesCommandExecutor.CommandType.HELM_INSTALL);
    verify(kubernetesManager, times(0))
        .helmInstall(defaultProvider.uuid, defaultUniverse.getUniverseDetails().nodePrefix);
    kubernetesCommandExecutor.run();
  }

  @Test
  public void testHelmDelete() {
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(KubernetesCommandExecutor.CommandType.HELM_DELETE);
    verify(kubernetesManager, times(0))
        .helmDelete(defaultProvider.uuid, defaultUniverse.getUniverseDetails().nodePrefix);
    kubernetesCommandExecutor.run();
  }

  @Test
  public void testVolumeDelete() {
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(KubernetesCommandExecutor.CommandType.VOLUME_DELETE);
    verify(kubernetesManager, times(0))
        .deleteStorage(defaultProvider.uuid, defaultUniverse.getUniverseDetails().nodePrefix);
    kubernetesCommandExecutor.run();
  }

  @Test
  public void testPodInfo() {
    ShellProcessHandler.ShellResponse shellResponse = new ShellProcessHandler.ShellResponse();
    shellResponse.message =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\"," +
            " \"podIP\": \"123.456.78.90\"}, \"spec\": {\"hostname\": \"yb-master-0\"}}," +
        "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"123.456.78.91\"}, \"spec\": {\"hostname\": \"yb-tserver-0\"}}," +
        "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"123.456.78.92\"}, \"spec\": {\"hostname\": \"yb-master-1\"}}," +
        "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\"," +
            " \"podIP\": \"123.456.78.93\"}, \"spec\": {\"hostname\": \"yb-tserver-1\"}}," +
        "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"123.456.78.94\"}, \"spec\": {\"hostname\": \"yb-master-2\"}}," +
        "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"123.456.78.95\"}, \"spec\": {\"hostname\": \"yb-tserver-2\"}}]}";
    when(kubernetesManager.getPodInfos(any(), any())).thenReturn(shellResponse);
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(KubernetesCommandExecutor.CommandType.POD_INFO);
    verify(kubernetesManager, times(0))
        .getPodInfos(defaultProvider.uuid, defaultUniverse.getUniverseDetails().nodePrefix);
    assertEquals(3, defaultUniverse.getNodes().size());
    kubernetesCommandExecutor.run();
    defaultUniverse = Universe.get(defaultUniverse.universeUUID);
    assertEquals(6, defaultUniverse.getNodes().size());
  }
}
