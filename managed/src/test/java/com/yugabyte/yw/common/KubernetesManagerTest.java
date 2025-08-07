// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.ImmutableList;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
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
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Captor;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.MockitoAnnotations;

@RunWith(JUnitParamsRunner.class)
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
    MockitoAnnotations.initMocks(this);
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
                    configProvider, "demo-az1", "demo-universe", true, false, universe.getName()));
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
        "There must be atleast one Master or TServer endpoint service, got 0",
        exception.getMessage());
  }

  @Test
  public void getTserverServiceIPs() {
    ShellResponse response = ShellResponse.create(0, "{\"items\": [{\"kind\": \"Service\"}]}");
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response);
    kubernetesManager.getPreferredServiceIP(
        configProvider, "demo-az2", "demo-universe", false, true, universe.getName());
    Mockito.verify(shellProcessHandler, times(1)).run(command.capture(), context.capture());
    assertEquals(
        ImmutableList.of(
            "kubectl",
            "get",
            "svc",
            "--namespace",
            "demo-universe",
            "-l",
            "app.kubernetes.io/part-of=testUniverse,app.kubernetes.io/name=yb-tserver,"
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

  @Test
  @Parameters({
    "kubernetes/statefulset_list_without_gflags_checksum.json",
    "kubernetes/statefulset_list_with_gflags_checksum.json",
  })
  public void testGetStatefulSetServerTypeGflagsChecksum(String outputFilePath) throws IOException {
    String kubectlResponse = TestUtils.readResource(outputFilePath);
    ShellResponse response = ShellResponse.create(0, kubectlResponse);
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response);
    Map<ServerType, String> serverTypeChecksumMap =
        kubernetesManager.getServerTypeGflagsChecksumMap(
            "test-ns", "test-release", configProvider, true /* newNamingStyle */);
    Mockito.verify(shellProcessHandler, times(1)).run(command.capture(), context.capture());
    assertEquals(
        ImmutableList.of(
            "kubectl",
            "get",
            "sts",
            "--namespace",
            "test-ns",
            "-o",
            "json",
            "-l",
            "release=" + "test-release"),
        command.getValue());

    // Verify checksum entries are as expected
    ObjectMapper mapper = new ObjectMapper();
    ArrayNode stsArray = (ArrayNode) mapper.readTree(kubectlResponse).get("items");
    for (JsonNode sts : stsArray) {
      JsonNode annotations = sts.get("spec").get("template").get("metadata").get("annotations");
      String expected =
          annotations.hasNonNull("checksum/gflags")
              ? annotations.get("checksum/gflags").asText()
              : "";
      if (sts.get("metadata")
          .get("labels")
          .get("app.kubernetes.io/name")
          .asText()
          .equals("yb-master")) {
        assertEquals(expected, serverTypeChecksumMap.get(ServerType.MASTER));
      } else {
        assertEquals(expected, serverTypeChecksumMap.get(ServerType.TSERVER));
      }
    }
  }

  @Test
  @Parameters({"kubernetes/statefulset_oldnaming_list_with_gflags_checksum.json"})
  public void testGetStatefulSetServerTypeGflagsChecksumOldNaming(String outputFilePath)
      throws IOException {
    String kubectlResponse = TestUtils.readResource(outputFilePath);
    ShellResponse response = ShellResponse.create(0, kubectlResponse);
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response);
    Map<ServerType, String> serverTypeChecksumMap =
        kubernetesManager.getServerTypeGflagsChecksumMap(
            "test-ns", "test-release", configProvider, false /* newNamingStyle */);
    Mockito.verify(shellProcessHandler, times(1)).run(command.capture(), context.capture());
    assertEquals(
        ImmutableList.of(
            "kubectl",
            "get",
            "sts",
            "--namespace",
            "test-ns",
            "-o",
            "json",
            "-l",
            "release=" + "test-release"),
        command.getValue());

    // Verify checksum entries are as expected
    ObjectMapper mapper = new ObjectMapper();
    ArrayNode stsArray = (ArrayNode) mapper.readTree(kubectlResponse).get("items");
    for (JsonNode sts : stsArray) {
      JsonNode annotations = sts.get("spec").get("template").get("metadata").get("annotations");
      String expected =
          annotations.hasNonNull("checksum/gflags")
              ? annotations.get("checksum/gflags").asText()
              : "";
      if (sts.get("metadata").get("labels").get("app").asText().equals("yb-master")) {
        assertEquals(expected, serverTypeChecksumMap.get(ServerType.MASTER));
      } else {
        assertEquals(expected, serverTypeChecksumMap.get(ServerType.TSERVER));
      }
    }
  }
}
