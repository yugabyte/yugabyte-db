// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Matchers.anyList;
import static org.mockito.Matchers.anyMap;
import static org.mockito.Matchers.anyString;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.runners.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class KubernetesManagerTest extends FakeDBApplication {

  @Mock ShellProcessHandler shellProcessHandler;

  @Mock play.Configuration mockAppConfig;

  @InjectMocks ShellKubernetesManager kubernetesManager;

  Provider defaultProvider;
  Customer defaultCustomer;

  ArgumentCaptor<ArrayList> command;
  ArgumentCaptor<HashMap> config;
  ArgumentCaptor<String> description;
  Map<String, String> configProvider = new HashMap<String, String>();

  static String TMP_CHART_PATH = "/tmp/yugaware_tests/KubernetesManagerTest/charts";

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.newProvider(defaultCustomer, Common.CloudType.kubernetes);
    configProvider.put("KUBECONFIG_SERVICE_ACCOUNT", "demo-account");
    configProvider.put("KUBECONFIG", "test");
    defaultProvider.setConfig(configProvider);
    defaultProvider.save();
    command = ArgumentCaptor.forClass(ArrayList.class);
    config = ArgumentCaptor.forClass(HashMap.class);
    description = ArgumentCaptor.forClass(String.class);
    new File(TMP_CHART_PATH).mkdirs();
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
    when(shellProcessHandler.run(anyList(), anyMap(), anyString())).thenReturn(response);

    int numOfCalls = 1;
    switch (commandType) {
      case HELM_INSTALL:
        kubernetesManager.helmInstall(
            ybSoftwareVersion,
            configProvider,
            defaultProvider.uuid,
            "demo-universe",
            "demo-namespace",
            "/tmp/override.yml",
            new HashMap<String, Object>(),
            new HashMap<String, Object>());
        break;
      case HELM_UPGRADE:
        kubernetesManager.helmUpgrade(
            ybSoftwareVersion,
            configProvider,
            "demo-universe",
            "demo-namespace",
            "/tmp/override.yml",
            new HashMap<String, Object>(),
            new HashMap<String, Object>());
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
        .run(command.capture(), (Map<String, String>) config.capture(), description.capture());
  }

  @Test
  public void testHelmUpgrade() {
    when(mockReleaseManager.getReleaseByVersion("2.8.0.0-b1"))
        .thenReturn(
            ReleaseManager.ReleaseMetadata.create("2.8.0.0-b1")
                .withChartPath(TMP_CHART_PATH + "/yugabyte-2.8.0.0-b1-helm.tar.gz"));
    createTempFile(TMP_CHART_PATH, "yugabyte-2.8.0.0-b1-helm.tar.gz", "Sample helm chart data");
    when(mockAppConfig.getLong("yb.helm.timeout_secs")).thenReturn((long) 600);
    runCommand(KubernetesCommandExecutor.CommandType.HELM_UPGRADE);
    assertEquals(
        ImmutableList.of(
            "helm",
            "upgrade",
            "demo-universe",
            "/tmp/yugaware_tests/KubernetesManagerTest/charts/yugabyte-2.8.0.0-b1-helm.tar.gz",
            "-f",
            "/tmp/override.yml",
            "--namespace",
            "demo-namespace",
            "--timeout",
            "600s",
            "--wait"),
        command.getValue());
    assertEquals(config.getValue(), configProvider);
  }

  @Test
  public void testHelmUpgradeNoTimeout() {
    when(mockReleaseManager.getReleaseByVersion("2.8.0.0-b1"))
        .thenReturn(
            ReleaseManager.ReleaseMetadata.create("2.8.0.0-b1")
                .withChartPath(TMP_CHART_PATH + "/yugabyte-2.8.0.0-b1-helm.tar.gz"));
    createTempFile(TMP_CHART_PATH, "yugabyte-2.8.0.0-b1-helm.tar.gz", "Sample helm chart data");
    runCommand(KubernetesCommandExecutor.CommandType.HELM_UPGRADE);
    assertEquals(
        ImmutableList.of(
            "helm",
            "upgrade",
            "demo-universe",
            "/tmp/yugaware_tests/KubernetesManagerTest/charts/yugabyte-2.8.0.0-b1-helm.tar.gz",
            "-f",
            "/tmp/override.yml",
            "--namespace",
            "demo-namespace",
            "--timeout",
            "300s",
            "--wait"),
        command.getValue());
    assertEquals(config.getValue(), configProvider);
  }

  @Test
  public void testHelmUpgradeFailWithNoConfig() {
    try {
      runCommand(KubernetesCommandExecutor.CommandType.HELM_UPGRADE);
    } catch (RuntimeException e) {
      assertEquals("Helm Package path not found for release: 2.8.0.0-b1", e.getMessage());
    }
  }

  @Test
  public void helmInstallWithRequiredConfig() {
    when(mockReleaseManager.getReleaseByVersion("2.8.0.0-b1"))
        .thenReturn(
            ReleaseManager.ReleaseMetadata.create("2.8.0.0-b1")
                .withChartPath(TMP_CHART_PATH + "/yugabyte-2.8.0.0-b1-helm.tar.gz"));
    createTempFile(TMP_CHART_PATH, "yugabyte-2.8.0.0-b1-helm.tar.gz", "Sample helm chart data");
    when(mockAppConfig.getLong("yb.helm.timeout_secs")).thenReturn((long) 600);
    runCommand(KubernetesCommandExecutor.CommandType.HELM_INSTALL);
    assertEquals(
        ImmutableList.of(
            "helm",
            "install",
            "demo-universe",
            "/tmp/yugaware_tests/KubernetesManagerTest/charts/yugabyte-2.8.0.0-b1-helm.tar.gz",
            "--namespace",
            "demo-namespace",
            "-f",
            "/tmp/override.yml",
            "--timeout",
            "600s",
            "--wait"),
        command.getValue());
    assertEquals(config.getValue(), configProvider);
  }

  @Test
  public void helmInstallWithNoTimeout() {
    when(mockReleaseManager.getReleaseByVersion("2.8.0.0-b1"))
        .thenReturn(
            ReleaseManager.ReleaseMetadata.create("2.8.0.0-b1")
                .withChartPath(TMP_CHART_PATH + "/yugabyte-2.8.0.0-b1-helm.tar.gz"));
    createTempFile(TMP_CHART_PATH, "yugabyte-2.8.0.0-b1-helm.tar.gz", "Sample helm chart data");
    runCommand(KubernetesCommandExecutor.CommandType.HELM_INSTALL);
    assertEquals(
        ImmutableList.of(
            "helm",
            "install",
            "demo-universe",
            "/tmp/yugaware_tests/KubernetesManagerTest/charts/yugabyte-2.8.0.0-b1-helm.tar.gz",
            "--namespace",
            "demo-namespace",
            "-f",
            "/tmp/override.yml",
            "--timeout",
            "300s",
            "--wait"),
        command.getValue());
    assertEquals(config.getValue(), configProvider);
  }

  @Test
  public void helmInstallWithoutRequiredConfig() {
    try {
      runCommand(KubernetesCommandExecutor.CommandType.HELM_INSTALL);
    } catch (RuntimeException e) {
      assertEquals("Helm Package path not found for release: 2.8.0.0-b1", e.getMessage());
    }
  }

  // TODO: Delete this test once all k8s customers are upgraded past 2.7 and legacy helm chart is no
  // longer required
  @Test
  public void helmInstallWithLegacyVersion() {
    when(mockAppConfig.getString("yb.helm.packagePath")).thenReturn(TMP_CHART_PATH);
    createTempFile(TMP_CHART_PATH, "yugabyte-2.7-helm-legacy.tar.gz", "Sample helm chart data");
    runCommand(KubernetesCommandExecutor.CommandType.HELM_INSTALL, "2.7.0.0-b1");
    assertEquals(
        ImmutableList.of(
            "helm",
            "install",
            "demo-universe",
            "/tmp/yugaware_tests/KubernetesManagerTest/charts/yugabyte-2.7-helm-legacy.tar.gz",
            "--namespace",
            "demo-namespace",
            "-f",
            "/tmp/override.yml",
            "--timeout",
            "300s",
            "--wait"),
        command.getValue());
    assertEquals(config.getValue(), configProvider);
  }

  @Test
  public void getPodInfos() {
    runCommand(KubernetesCommandExecutor.CommandType.POD_INFO);
    assertEquals(
        ImmutableList.of(
            "kubectl",
            "get",
            "pods",
            "--namespace",
            "demo-namespace",
            "-o",
            "json",
            "-l",
            "release=demo-universe"),
        command.getValue());
    assertEquals(config.getValue(), configProvider);
  }

  @Test
  public void helmDelete() {
    runCommand(KubernetesCommandExecutor.CommandType.HELM_DELETE);
    assertEquals(
        ImmutableList.of("helm", "delete", "demo-universe", "-n", "demo-namespace"),
        command.getValue());
    assertEquals(config.getValue(), configProvider);
  }

  @Test
  public void deleteStorage() {
    runCommand(KubernetesCommandExecutor.CommandType.VOLUME_DELETE);
    assertEquals(
        ImmutableList.of(
            ImmutableList.of(
                "kubectl",
                "delete",
                "pvc",
                "--namespace",
                "demo-namespace",
                "-l",
                "release=demo-universe")),
        command.getAllValues());
    assertEquals(config.getValue(), configProvider);
  }

  @Test
  public void getMasterServiceIPs() {
    ShellResponse response = ShellResponse.create(0, "{}");
    when(shellProcessHandler.run(anyList(), anyMap(), anyString())).thenReturn(response);
    Throwable exception =
        assertThrows(
            RuntimeException.class,
            () ->
                kubernetesManager.getPreferredServiceIP(
                    configProvider, "demo-az1", "demo-universe", true, false));
    Mockito.verify(shellProcessHandler, times(1))
        .run(command.capture(), (Map<String, String>) config.capture(), description.capture());
    assertEquals(
        ImmutableList.of(
            "kubectl",
            "get",
            "svc",
            "--namespace",
            "demo-universe",
            "-l",
            "release=demo-az1,app=yb-master,service-type!=headless",
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
    when(shellProcessHandler.run(anyList(), anyMap(), anyString())).thenReturn(response);
    kubernetesManager.getPreferredServiceIP(
        configProvider, "demo-az2", "demo-universe", false, true);
    Mockito.verify(shellProcessHandler, times(1))
        .run(command.capture(), (Map<String, String>) config.capture(), description.capture());
    assertEquals(
        ImmutableList.of(
            "kubectl",
            "get",
            "svc",
            "--namespace",
            "demo-universe",
            "-l",
            "release=demo-az2,app.kubernetes.io/name=yb-tserver,service-type!=headless",
            "-o",
            "json"),
        command.getValue());
  }

  @Test
  public void getServices() {
    ShellResponse response = ShellResponse.create(0, "{}");
    when(shellProcessHandler.run(anyList(), anyMap(), anyString())).thenReturn(response);
    kubernetesManager.getServices(configProvider, "demo-universe", "demo-ns");
    Mockito.verify(shellProcessHandler, times(1))
        .run(command.capture(), (Map<String, String>) config.capture(), description.capture());
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

  @Test
  public void getNodeInfos() {
    ShellResponse response = ShellResponse.create(0, "{}");
    when(shellProcessHandler.run(anyList(), anyMap(), anyString())).thenReturn(response);
    kubernetesManager.getNodeInfos(configProvider);
    Mockito.verify(shellProcessHandler, times(1))
        .run(command.capture(), (Map<String, String>) config.capture(), description.capture());
    assertEquals(ImmutableList.of("kubectl", "get", "nodes", "-o", "json"), command.getValue());
  }

  @Test
  public void getSecret() {
    ShellResponse response = ShellResponse.create(0, "{}");
    when(shellProcessHandler.run(anyList(), anyMap(), anyString())).thenReturn(response);
    kubernetesManager.getSecret(configProvider, "pull-sec", "test-ns");
    Mockito.verify(shellProcessHandler, times(1))
        .run(command.capture(), (Map<String, String>) config.capture(), description.capture());
    assertEquals(
        ImmutableList.of(
            "kubectl", "get", "secret", "pull-sec", "-o", "json", "--namespace", "test-ns"),
        command.getValue());
  }

  @Test
  public void getSecretWithoutNamespace() {
    ShellResponse response = ShellResponse.create(0, "{}");
    when(shellProcessHandler.run(anyList(), anyMap(), anyString())).thenReturn(response);
    kubernetesManager.getSecret(configProvider, "pull-sec", null);
    Mockito.verify(shellProcessHandler, times(1))
        .run(command.capture(), (Map<String, String>) config.capture(), description.capture());
    assertEquals(
        ImmutableList.of("kubectl", "get", "secret", "pull-sec", "-o", "json"), command.getValue());
  }
}
