// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import java.io.File;
import java.io.IOException;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Captor;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class KubernetesManagerTest extends FakeDBApplication {

  @Mock ShellProcessHandler shellProcessHandler;

  @Mock RuntimeConfGetter mockConfGetter;

  ShellKubernetesManager kubernetesManager;

  @Mock Config mockAppConfig;

  Provider defaultProvider;
  Customer defaultCustomer;
  Universe universe;
  ReleaseManager releaseManager;

  @Captor ArgumentCaptor<List<String>> command;

  @Captor ArgumentCaptor<ShellProcessContext> context;
  Map<String, String> configProvider = new HashMap<String, String>();

  static String TMP_CHART_PATH = "/tmp/yugaware_tests/KubernetesManagerTest/charts";

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.newProvider(defaultCustomer, Common.CloudType.kubernetes);
    universe = ModelFactory.createUniverse("testUniverse", defaultCustomer.getId());
    configProvider.put("KUBECONFIG", "test");
    defaultProvider.setConfigMap(configProvider);
    defaultProvider.save();
    new File(TMP_CHART_PATH).mkdirs();
    releaseManager = app.injector().instanceOf(ReleaseManager.class);
    kubernetesManager = new ShellKubernetesManager(shellProcessHandler);
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(TMP_CHART_PATH));
  }

  private void runCommand(KubernetesCommandExecutor.CommandType commandType) {
    runCommand(commandType, "2.8.0.0-b1");
  }

  private void runCommand(
      KubernetesCommandExecutor.CommandType commandType, String ybSoftwareVersion) {
    ShellResponse response = ShellResponse.create(0, "{}");
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response);

    int numOfCalls = 1;
    switch (commandType) {
      case HELM_INSTALL:
        kubernetesManager.helmInstall(
            universe.getUniverseUUID(),
            ybSoftwareVersion,
            configProvider,
            defaultProvider.getUuid(),
            "demo-universe",
            "demo-namespace",
            "/tmp/override.yml");
        break;
      case HELM_UPGRADE:
        kubernetesManager.helmUpgrade(
            universe.getUniverseUUID(),
            ybSoftwareVersion,
            configProvider,
            "demo-universe",
            "demo-namespace",
            "/tmp/override.yml");
        break;
      case POD_INFO:
        kubernetesManager.getPodInfos(configProvider, "demo-universe", "demo-namespace");
        break;
      case HELM_DELETE:
        kubernetesManager.helmDelete(configProvider, "demo-universe", "demo-namespace");
        break;
      case VOLUME_DELETE:
        kubernetesManager.deleteStorage(configProvider, "demo-universe", "demo-namespace");
        break;
    }

    Mockito.verify(shellProcessHandler, times(numOfCalls))
        .run(command.capture(), context.capture());
  }

  @Test
  public void getMasterServiceIPs() {
    ShellResponse response = ShellResponse.create(0, "{}");
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response);
    Throwable exception =
        assertThrows(
            RuntimeException.class,
            () ->
                kubernetesManager.getPreferredServiceIP(
                    configProvider, "demo-az1", "demo-universe", true, false));
    Mockito.verify(shellProcessHandler, times(1)).run(command.capture(), context.capture());
    assertEquals(
        ImmutableList.of(
            "kubectl",
            "get",
            "svc",
            "--namespace",
            "demo-universe",
            "-l",
            "release=demo-az1,app=yb-master,service-type notin (headless, non-endpoint)",
            "-o",
            "json"),
        command.getValue());
    assertEquals(
        "There must be exactly one Master or TServer endpoint service, got 0",
        exception.getMessage());
  }

  @Test
  public void getTserverServiceIPs() {
    ShellResponse response = ShellResponse.create(0, "{\"items\": [{\"kind\": \"Service\"}]}");
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response);
    kubernetesManager.getPreferredServiceIP(
        configProvider, "demo-az2", "demo-universe", false, true);
    Mockito.verify(shellProcessHandler, times(1)).run(command.capture(), context.capture());
    assertEquals(
        ImmutableList.of(
            "kubectl",
            "get",
            "svc",
            "--namespace",
            "demo-universe",
            "-l",
            "release=demo-az2,app.kubernetes.io/name=yb-tserver,"
                + "service-type notin (headless, non-endpoint)",
            "-o",
            "json"),
        command.getValue());
  }

  @Test
  public void getServices() {
    ShellResponse response = ShellResponse.create(0, "{}");
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response);
    kubernetesManager.getServices(configProvider, "demo-universe", "demo-ns");
    Mockito.verify(shellProcessHandler, times(1)).run(command.capture(), context.capture());
    assertEquals(
        ImmutableList.of(
            "kubectl",
            "get",
            "services",
            "--namespace",
            "demo-ns",
            "-o",
            "json",
            "-l",
            "release=" + "demo-universe"),
        command.getValue());
  }
}
