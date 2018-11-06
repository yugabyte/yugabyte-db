// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.runners.MockitoJUnitRunner;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;

import static org.junit.Assert.*;
import static org.mockito.Matchers.anyList;
import static org.mockito.Matchers.anyMap;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;

@RunWith(MockitoJUnitRunner.class)
public class KubernetesManagerTest extends FakeDBApplication {

  @Mock
  ShellProcessHandler shellProcessHandler;

  @Mock
  play.Configuration mockAppConfig;

  @InjectMocks
  KubernetesManager kubernetesManager;

  Provider defaultProvider;
  Customer defaultCustomer;


  ArgumentCaptor<ArrayList> command;
  ArgumentCaptor<HashMap> config;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.newProvider(defaultCustomer, Common.CloudType.kubernetes);
    command = ArgumentCaptor.forClass(ArrayList.class);
    config = ArgumentCaptor.forClass(HashMap.class);
  }

  private void runCommand(KubernetesCommandExecutor.CommandType commandType) {
    ShellProcessHandler.ShellResponse response = new ShellProcessHandler.ShellResponse();
    when(shellProcessHandler.run(anyList(), anyMap())).thenReturn(response);

    int numOfCalls = 1;
    switch(commandType) {
      case HELM_INIT:
        kubernetesManager.helmInit(defaultProvider.uuid);
        break;
      case HELM_INSTALL:
        kubernetesManager.helmInstall(defaultProvider.uuid, "demo-universe",
            "/tmp/override.yml");
        break;
      case POD_INFO:
        kubernetesManager.getPodInfos(defaultProvider.uuid, "demo-universe");
        break;
      case HELM_DELETE:
        kubernetesManager.helmDelete(defaultProvider.uuid, "demo-universe");
        break;
      case VOLUME_DELETE:
        kubernetesManager.deleteStorage(defaultProvider.uuid, "demo-universe");
        numOfCalls = 2;
        break;
    }

    Mockito.verify(shellProcessHandler, times(numOfCalls)).run(command.capture(),
        (Map<String, String>) config.capture());
  }

  @Test
  public void testHelmInitWithRequiredConfig() {
    Map<String, String> providerConfig = new HashMap<>();
    providerConfig.put("KUBECONFIG_SERVICE_ACCOUNT", "demo-account");
    defaultProvider.setConfig(providerConfig);
    defaultProvider.save();
    runCommand(KubernetesCommandExecutor.CommandType.HELM_INIT);
    assertEquals(providerConfig, config.getValue());
    assertEquals(ImmutableList.of("helm", "init", "--service-account", "demo-account", "--upgrade", "--wait"),
        command.getValue());
  }

  @Test
  public void testHelmInitWithoutRequiredConfig() {
    try {
      runCommand(KubernetesCommandExecutor.CommandType.HELM_INIT);
    } catch (RuntimeException e) {
      assertEquals("Service Account is required.", e.getMessage());
    }
  }


  @Test
  public void helmInstallWithRequiredConfig() {
    when(mockAppConfig.getString("yb.helm.package")).thenReturn("/my/helm.tgz");
    runCommand(KubernetesCommandExecutor.CommandType.HELM_INSTALL);
    assertEquals(ImmutableList.of("helm", "install", "/my/helm.tgz",
        "--namespace", "demo-universe", "--name", "demo-universe", "-f",
        "/tmp/override.yml", "--wait"),
        command.getValue());
    assertTrue(config.getValue().isEmpty());
  }

  @Test
  public void helmInstallWithoutRequiredConfig() {
    try {
      runCommand(KubernetesCommandExecutor.CommandType.HELM_INSTALL);
    } catch (RuntimeException e) {
      assertEquals("Helm Package path not provided.", e.getMessage());
    }
  }

  @Test
  public void getPodInfos() {
    runCommand(KubernetesCommandExecutor.CommandType.POD_INFO);
    assertEquals(ImmutableList.of("kubectl", "get", "pods",
        "--namespace", "demo-universe", "-o", "json",  "-l", "release=demo-universe"),
        command.getValue());
    assertTrue(config.getValue().isEmpty());
  }

  @Test
  public void helmDelete() {
    runCommand(KubernetesCommandExecutor.CommandType.HELM_DELETE);
    assertEquals(ImmutableList.of("helm", "delete", "demo-universe", "--purge"),
        command.getValue());
    assertTrue(config.getValue().isEmpty());
  }

  @Test
  public void deleteStorage() {
    runCommand(KubernetesCommandExecutor.CommandType.VOLUME_DELETE);
    assertEquals(ImmutableList.of(
        ImmutableList.of("kubectl", "delete", "pvc",
            "--namespace", "demo-universe", "-l", "app=yb-master"),
        ImmutableList.of("kubectl", "delete", "pvc",
            "--namespace", "demo-universe", "-l", "app=yb-tserver")),
        command.getAllValues());
    assertTrue(config.getValue().isEmpty());
  }

  @Test
  public void getMasterServiceIPs() {
      kubernetesManager.getServiceIPs(defaultProvider.uuid, "demo-universe", true);
      Mockito.verify(shellProcessHandler, times(1))
          .run(command.capture(), (Map<String, String>) config.capture());
      assertEquals(ImmutableList.of("kubectl", "get", "svc",
          "yb-master-service", "--namespace", "demo-universe", "-o",
          "jsonpath={.spec.clusterIP}|{.status.*.ingress[0].ip}|{.status.*.ingress[0].hostname}"), command.getValue());
  }

  @Test
  public void getTserverServiceIPs() {
    kubernetesManager.getServiceIPs(defaultProvider.uuid, "demo-universe", false);
    Mockito.verify(shellProcessHandler, times(1))
        .run(command.capture(), (Map<String, String>) config.capture());
    assertEquals(ImmutableList.of("kubectl", "get", "svc",
        "yb-tserver-service", "--namespace", "demo-universe", "-o",
        "jsonpath={.spec.clusterIP}|{.status.*.ingress[0].ip}|{.status.*.ingress[0].hostname}"), command.getValue());
  }
}
