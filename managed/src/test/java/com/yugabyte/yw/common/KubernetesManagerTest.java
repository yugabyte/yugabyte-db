// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
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
import org.mockito.Mockito.*;
import org.mockito.MockitoAnnotations;
import org.yaml.snakeyaml.Yaml;

@RunWith(JUnitParamsRunner.class)
public class KubernetesManagerTest extends FakeDBApplication {

  @Mock ShellProcessHandler shellProcessHandler;
  @Mock FileHelperService fileHelperService;
  @Mock RuntimeConfGetter mockConfGetter;

  ShellKubernetesManager kubernetesManager;

  @Mock Config mockAppConfig;

  Provider defaultProvider;
  Customer defaultCustomer;
  Universe universe;
  Provider kubernetesProvider;
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
    kubernetesManager = new ShellKubernetesManager(shellProcessHandler, fileHelperService);
    kubernetesProvider = ModelFactory.kubernetesProvider(defaultCustomer);
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

  @Test
  public void testCheckStatefulSetStatus_Failure_ReplicaMismatch() {
    ShellResponse response1 = ShellResponse.create(0, "statefulset1");
    ShellResponse response2 =
        ShellResponse.create(0, "replicas=3|readyReplicas=1|availableReplicas=1");
    Map<String, String> testConfig = new HashMap<String, String>();
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response1);
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response2);
    boolean status =
        kubernetesManager.checkStatefulSetStatus(testConfig, "test-ns", "test-release", 3);
    assertEquals(false, status);
  }

  @Test
  public void testCheckStatefulSetStatus_success() {
    ShellResponse response1 = ShellResponse.create(0, "statefulset1");
    ShellResponse response2 =
        ShellResponse.create(0, "replicas=3|readyReplicas=3|availableReplicas=3");
    Map<String, String> testConfig = new HashMap<String, String>();
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response1);
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response2);
    boolean status =
        kubernetesManager.checkStatefulSetStatus(testConfig, "test-ns", "test-release", 3);
    assertEquals(true, status);
  }

  @Test
  public void testCheckStatefulSetStatus_success_malformed_1() {
    ShellResponse response1 = ShellResponse.create(0, "statefulset1");
    ShellResponse response2 =
        ShellResponse.create(0, "replicas=3|readyReplicas=3|availableReplicas=");
    Map<String, String> testConfig = new HashMap<String, String>();
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response1);
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response2);
    boolean status =
        kubernetesManager.checkStatefulSetStatus(testConfig, "test-ns", "test-release", 3);
    assertEquals(true, status);
  }

  @Test
  public void testCheckStatefulSetStatus_success_malformed_2() {
    ShellResponse response1 = ShellResponse.create(0, "statefulset1");
    ShellResponse response2 =
        ShellResponse.create(0, "replicas=3|readyReplicas=|availableReplicas=3");
    Map<String, String> testConfig = new HashMap<String, String>();
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response1);
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response2);
    boolean status =
        kubernetesManager.checkStatefulSetStatus(testConfig, "test-ns", "test-release", 3);
    assertEquals(true, status);
  }

  @Test
  public void testCheckStatefulSetStatus_success_no_replicas() {
    ShellResponse response1 = ShellResponse.create(0, "statefulset1");
    ShellResponse response2 =
        ShellResponse.create(0, "replicas=0|readyReplicas=|availableReplicas=3");
    Map<String, String> testConfig = new HashMap<String, String>();
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response1);
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response2);
    boolean status =
        kubernetesManager.checkStatefulSetStatus(testConfig, "test-ns", "test-release", 3);
    assertEquals(true, status);
  }

  private void setupUniverse() {
    kubernetesProvider.setConfigMap(ImmutableMap.of("KUBECONFIG", "test"));
    kubernetesProvider.save();
    Region r = Region.create(kubernetesProvider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone az = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");
    InstanceType i =
        InstanceType.upsert(
            kubernetesProvider.getUuid(),
            "c3.xlarge",
            10,
            5.5,
            new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getTestUserIntent(r, kubernetesProvider, i, 3);
    userIntent.replicationFactor = 3;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags = new HashMap<>();
    userIntent.universeName = "demo-universe";
    userIntent.ybSoftwareVersion = "1.0.0";
    universe = createUniverse("test-universe", defaultCustomer.getId());
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, "demo-universe", false /* setMasters */));
    UniverseDefinitionTaskParams.UserIntent userIntentReadReplica = userIntent.clone();
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az.getUuid(), pi, 1, 1, false);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntentReadReplica, pi));
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            (universe) -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.useNewHelmNamingStyle = true;
              universe.setUniverseDetails(universeDetails);
            },
            false);

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    universe.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    universe.save();
  }

  private void setupUniverseMultiAZ() {
    Region r = Region.create(kubernetesProvider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ-2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ-3", "subnet-3");

    String nodePrefix1 = String.format("%s-%s", "demo-universe", az1.getCode());
    String nodePrefix2 = String.format("%s-%s", "demo-universe", az2.getCode());
    String nodePrefix3 = String.format("%s-%s", "demo-universe", az3.getCode());
    String ns1 = nodePrefix1;
    String ns2 = nodePrefix2;
    String ns3 = nodePrefix3;
    az1.updateConfig(ImmutableMap.of("KUBECONFIG", "test-kc-" + 1, "KUBENAMESPACE", ns1));
    az1.save();
    az2.updateConfig(ImmutableMap.of("KUBECONFIG", "test-kc-" + 2, "KUBENAMESPACE", ns2));
    az2.save();
    az3.updateConfig(ImmutableMap.of("KUBECONFIG", "test-kc-" + 3, "KUBENAMESPACE", ns3));
    az3.save();
    InstanceType i =
        InstanceType.upsert(
            kubernetesProvider.getUuid(),
            "c3.xlarge",
            10,
            5.5,
            new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getTestUserIntent(r, kubernetesProvider, i, 3);
    userIntent.replicationFactor = 3;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags = new HashMap<>();
    userIntent.universeName = "demo-universe";
    userIntent.ybSoftwareVersion = "1.0.0";
    universe = createUniverse("test-universe", defaultCustomer.getId());
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            (universe) -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.useNewHelmNamingStyle = true;
              universe.setUniverseDetails(universeDetails);
            },
            false);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, "demo-universe", false /* setMasters */));
    UniverseDefinitionTaskParams.UserIntent userIntentReadReplica = userIntent.clone();
    PlacementInfo pi = new PlacementInfo();
    for (AvailabilityZone a : r.getAllZones()) {
      PlacementInfoUtil.addPlacementZone(a.getUuid(), pi, 1, 1, false);
    }
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntentReadReplica, pi));
    universe.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    universe.save();
  }

  private void validatePDBApplyCommand(
      Universe universe, List<String> policyFiles, List<String> namespaceList) {
    List<List<String>> expectedShellCommands = new ArrayList<>();
    for (String namespace : namespaceList) {
      for (String policyFile : policyFiles) {
        expectedShellCommands.add(
            ImmutableList.of("kubectl", "apply", "-f", policyFile, "--namespace", namespace));
      }
    }
    List<String> capturedCommands =
        command.getAllValues().stream().map(c -> String.join(" ", c)).collect(Collectors.toList());
    List<String> expectedCommands =
        expectedShellCommands.stream().map(c -> String.join(" ", c)).collect(Collectors.toList());
    assertEquals(expectedCommands.size(), capturedCommands.size());
    for (String expectedCommand : expectedCommands) {
      assertTrue(capturedCommands.contains(expectedCommand));
    }
  }

  private void validatePolicyFiles(Universe universe, List<String> policyFiles) {
    for (String policyFile : policyFiles) {
      ServerType serverType =
          policyFile.contains("master") ? ServerType.MASTER : ServerType.TSERVER;
      ClusterType clusterType =
          policyFile.contains("primary") ? ClusterType.PRIMARY : ClusterType.ASYNC;

      List<UniverseDefinitionTaskParams.Cluster> clusters =
          universe.getUniverseDetails().getClusterByType(clusterType);
      UniverseDefinitionTaskParams.Cluster cluster = clusters.isEmpty() ? null : clusters.get(0);

      Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
      Set<String> helmReleaseNames = new HashSet<>();
      boolean isMultiAZ = PlacementInfoUtil.isMultiAZ(provider);
      for (NodeDetails node : universe.getNodesInCluster(cluster.uuid)) {
        AvailabilityZone az = AvailabilityZone.getOrBadRequest(node.azUuid);
        String helmRelease =
            KubernetesUtil.getHelmReleaseName(
                isMultiAZ,
                universe.getUniverseDetails().nodePrefix,
                universe.getName(),
                az.getName(),
                cluster.clusterType == ClusterType.ASYNC,
                universe.getUniverseDetails().useNewHelmNamingStyle);
        helmReleaseNames.add(helmRelease);
      }

      try (FileInputStream inputStream = new FileInputStream(policyFile)) {
        Yaml yaml = new Yaml();
        Map<String, Object> obj = yaml.load(inputStream);
        assertEquals("policy/v1", obj.get("apiVersion"));
        assertEquals("PodDisruptionBudget", obj.get("kind"));
        Map<String, Object> metadata = (Map<String, Object>) obj.get("metadata");
        assertEquals(
            String.format(
                "%s-%s-%s-pdb",
                universe.getName(),
                cluster.clusterType.equals(ClusterType.PRIMARY) ? "primary" : "replica",
                serverType.toString().toLowerCase()),
            metadata.get("name").toString());
        Map<String, Object> spec = (Map<String, Object>) obj.get("spec");
        assertEquals(cluster.userIntent.replicationFactor / 2, spec.get("maxUnavailable"));
        Map<String, Object> selector = (Map<String, Object>) spec.get("selector");
        Map<String, Object> matchLabels = (Map<String, Object>) selector.get("matchLabels");
        assertEquals(
            String.format("yb-%s", serverType.toString().toLowerCase()),
            matchLabels.get("app.kubernetes.io/name").toString());
        List<Object> matchExpressions = (List<Object>) selector.get("matchExpressions");
        Map<String, Object> matchExpression = (Map<String, Object>) matchExpressions.get(0);
        assertEquals("release", matchExpression.get("key"));
        assertEquals("In", matchExpression.get("operator"));
        List<String> values = (List<String>) matchExpression.get("values");
        assertEquals(helmReleaseNames.size(), values.size());
        for (String helmRelease : helmReleaseNames) {
          assertTrue(values.contains(helmRelease));
        }
      } catch (IOException e) {
        e.printStackTrace();
        fail();
      }
    }
  }

  @Test
  public void testCreatePodDisruptionBudget() {
    setupUniverse();
    List<String> policyFiles = new ArrayList<>();
    when(fileHelperService.createTempFile(anyString(), anyString()))
        .thenAnswer(
            i -> {
              String fileName = i.getArgument(0);
              String fileExtension = i.getArgument(1);
              Path path = Files.createTempFile(Paths.get("/tmp"), fileName, fileExtension);
              policyFiles.add(path.toString());
              return path;
            });
    ShellResponse response = ShellResponse.create(0, "{}");
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response);
    kubernetesManager.createPodDisruptionBudget(universe);
    Mockito.verify(shellProcessHandler, times(3)).run(command.capture(), context.capture());
    assertEquals(3, policyFiles.size());
    validatePolicyFiles(universe, policyFiles);
    validatePDBApplyCommand(universe, policyFiles, ImmutableList.of("demo-universe"));
  }

  @Test
  public void testCreatePodDisruptionBudgetForMultiAZUniverse() {
    setupUniverseMultiAZ();
    List<String> policyFiles = new ArrayList<>();
    when(fileHelperService.createTempFile(anyString(), anyString()))
        .thenAnswer(
            i -> {
              String fileName = i.getArgument(0);
              String fileExtension = i.getArgument(1);
              Path path = Files.createTempFile(Paths.get("/tmp"), fileName, fileExtension);
              policyFiles.add(path.toString());
              return path;
            });
    ShellResponse response = ShellResponse.create(0, "{}");
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response);
    kubernetesManager.createPodDisruptionBudget(universe);
    Mockito.verify(shellProcessHandler, times(9)).run(command.capture(), context.capture());
    List<String> namespaceList =
        ImmutableList.of("demo-universe-az-1", "demo-universe-az-2", "demo-universe-az-3");
    assertEquals(3, policyFiles.size());
    validatePolicyFiles(universe, policyFiles);
    validatePDBApplyCommand(universe, policyFiles, namespaceList);
  }

  private void validateDeletePDBCommand(Universe universe, List<String> namespaceList) {
    List<List<String>> expectedShellCommands = new ArrayList<>();
    for (String namespace : namespaceList) {
      for (Cluster cluster : universe.getUniverseDetails().clusters) {
        for (ServerType serverType : ImmutableList.of(ServerType.MASTER, ServerType.TSERVER)) {
          if (serverType.equals(ServerType.MASTER)
              && cluster.clusterType.equals(ClusterType.ASYNC)) {
            continue;
          }
          expectedShellCommands.add(
              ImmutableList.of(
                  "kubectl",
                  "delete",
                  "pdb",
                  String.format(
                      "%s-%s-%s-pdb",
                      universe.getName(),
                      cluster.clusterType.equals(ClusterType.PRIMARY) ? "primary" : "replica",
                      serverType.toString().toLowerCase()),
                  "--namespace",
                  namespace,
                  "--ignore-not-found"));
        }
      }
    }
    List<String> capturedCommands =
        command.getAllValues().stream().map(c -> String.join(" ", c)).collect(Collectors.toList());
    List<String> expectedCommands =
        expectedShellCommands.stream().map(c -> String.join(" ", c)).collect(Collectors.toList());
    assertEquals(expectedCommands.size(), capturedCommands.size());
    for (String expectedCommand : expectedCommands) {
      assertTrue(capturedCommands.contains(expectedCommand));
    }
  }

  @Test
  public void testDeletePodDisruptionBudget() {
    setupUniverse();
    ShellResponse response = ShellResponse.create(0, "{}");
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response);
    kubernetesManager.deletePodDisruptionBudget(universe);
    Mockito.verify(shellProcessHandler, times(3)).run(command.capture(), context.capture());
    validateDeletePDBCommand(universe, ImmutableList.of("demo-universe"));
  }

  @Test
  public void testDeletePodDisruptionBudgetInMultiAZUniverse() {
    setupUniverseMultiAZ();
    ShellResponse response = ShellResponse.create(0, "{}");
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response);
    kubernetesManager.deletePodDisruptionBudget(universe);
    Mockito.verify(shellProcessHandler, times(9)).run(command.capture(), context.capture());
    List<String> namespaceList =
        ImmutableList.of("demo-universe-az-1", "demo-universe-az-2", "demo-universe-az-3");
    validateDeletePDBCommand(universe, namespaceList);
  }

  private void setupKubernetesManagerForHelmTests() {
    // Set parent class fields (KubernetesManager) using reflection since ShellKubernetesManager
    // has private fields with the same names that hide the parent fields
    try {
      Class<?> parentClass = KubernetesManager.class;
      java.lang.reflect.Field appConfigField = parentClass.getDeclaredField("appConfig");
      appConfigField.setAccessible(true);
      appConfigField.set(kubernetesManager, mockAppConfig);

      java.lang.reflect.Field confGetterField = parentClass.getDeclaredField("confGetter");
      confGetterField.setAccessible(true);
      confGetterField.set(kubernetesManager, mockConfGetter);

      java.lang.reflect.Field releaseManagerField = parentClass.getDeclaredField("releaseManager");
      releaseManagerField.setAccessible(true);
      releaseManagerField.set(kubernetesManager, releaseManager);

      java.lang.reflect.Field shellProcessHandlerField =
          parentClass.getDeclaredField("shellProcessHandler");
      shellProcessHandlerField.setAccessible(true);
      shellProcessHandlerField.set(kubernetesManager, shellProcessHandler);

      java.lang.reflect.Field fileHelperServiceField =
          parentClass.getDeclaredField("fileHelperService");
      fileHelperServiceField.setAccessible(true);
      fileHelperServiceField.set(kubernetesManager, fileHelperService);
    } catch (Exception e) {
      throw new RuntimeException("Failed to set up KubernetesManager fields", e);
    }
  }

  @Test
  public void testHelmInstallWithCommonLabels() throws IOException {
    // Create a temporary override file with commonLabels
    Path overrideFile = Files.createTempFile("test-overrides", ".yml");
    String yamlContent =
        "commonLabels:\n"
            + "  environment: production\n"
            + "  team: platform\n"
            + "  cost-center: data-infra\n"
            + "replicas: 3\n";
    Files.write(overrideFile, yamlContent.getBytes());

    // Create a dummy helm chart file for versions < 2.8.0.0
    File helmChartFile = new File(TMP_CHART_PATH, "yugabyte-2.7-helm-legacy.tar.gz");
    helmChartFile.getParentFile().mkdirs();
    helmChartFile.createNewFile();

    try {
      setupKubernetesManagerForHelmTests();
      // Mock helm list to return empty (no existing release)
      ShellResponse listResponse = ShellResponse.create(0, "");
      // Mock helm install response
      ShellResponse installResponse = ShellResponse.create(0, "Release installed");
      // Mock the 5-parameter run method that execCommand uses
      when(shellProcessHandler.run(
              anyList(),
              anyMap(),
              anyBoolean(),
              anyString(),
              any(RedactingService.RedactionTarget.class)))
          .thenReturn(listResponse)
          .thenReturn(installResponse);

      // Mock getHelmPackagePath - we need to set up a release
      when(mockAppConfig.getString("yb.helm.packagePath")).thenReturn(TMP_CHART_PATH);
      when(mockConfGetter.getConfForScope(any(Universe.class), any())).thenReturn(300L); // timeout

      kubernetesManager.helmInstall(
          universe.getUniverseUUID(),
          "2.7.0.0-b1", // Use version < 2.8.0.0 to use legacy chart
          configProvider,
          defaultProvider.getUuid(),
          "demo-universe",
          "demo-namespace",
          overrideFile.toString());

      // Verify helm install was called with --labels flag
      // Note: execCommand uses the 5-parameter run method
      ArgumentCaptor<List<String>> commandCaptor = ArgumentCaptor.forClass(List.class);
      ArgumentCaptor<Map<String, String>> configCaptor = ArgumentCaptor.forClass(Map.class);
      Mockito.verify(shellProcessHandler, times(2))
          .run(
              commandCaptor.capture(),
              configCaptor.capture(),
              anyBoolean(),
              anyString(),
              any(RedactingService.RedactionTarget.class));
      List<List<String>> allCommands = commandCaptor.getAllValues();
      // The second call should be helm install
      List<String> installCommand = allCommands.get(1);
      assertTrue(
          "helm install command should contain --labels flag", installCommand.contains("--labels"));
      int labelsIndex = installCommand.indexOf("--labels");
      assertTrue("--labels flag should have a value", labelsIndex < installCommand.size() - 1);
      String labelsValue = installCommand.get(labelsIndex + 1);
      assertTrue(
          "Labels should contain environment=production",
          labelsValue.contains("environment=production"));
      assertTrue("Labels should contain team=platform", labelsValue.contains("team=platform"));
      assertTrue(
          "Labels should contain cost-center=data-infra",
          labelsValue.contains("cost-center=data-infra"));
    } finally {
      Files.deleteIfExists(overrideFile);
      helmChartFile.delete();
    }
  }

  @Test
  public void testHelmInstallWithoutCommonLabels() throws IOException {
    // Create a temporary override file without commonLabels
    Path overrideFile = Files.createTempFile("test-overrides", ".yml");
    String yamlContent = "replicas: 3\n" + "resources:\n" + "  limits:\n" + "    memory: 2Gi\n";
    Files.write(overrideFile, yamlContent.getBytes());

    // Create a dummy helm chart file for versions < 2.8.0.0
    File helmChartFile = new File(TMP_CHART_PATH, "yugabyte-2.7-helm-legacy.tar.gz");
    helmChartFile.getParentFile().mkdirs();
    helmChartFile.createNewFile();

    try {
      setupKubernetesManagerForHelmTests();
      // Mock helm list to return empty (no existing release)
      ShellResponse listResponse = ShellResponse.create(0, "");
      // Mock helm install response
      ShellResponse installResponse = ShellResponse.create(0, "Release installed");
      // Mock the 5-parameter run method that execCommand uses
      when(shellProcessHandler.run(
              anyList(),
              anyMap(),
              anyBoolean(),
              anyString(),
              any(RedactingService.RedactionTarget.class)))
          .thenReturn(listResponse)
          .thenReturn(installResponse);

      // Mock getHelmPackagePath
      when(mockAppConfig.getString("yb.helm.packagePath")).thenReturn(TMP_CHART_PATH);
      when(mockConfGetter.getConfForScope(any(Universe.class), any())).thenReturn(300L);

      kubernetesManager.helmInstall(
          universe.getUniverseUUID(),
          "2.7.0.0-b1", // Use version < 2.8.0.0 to use legacy chart
          configProvider,
          defaultProvider.getUuid(),
          "demo-universe",
          "demo-namespace",
          overrideFile.toString());

      // Verify helm install was called without --labels flag
      // Note: execCommand uses the 5-parameter run method
      ArgumentCaptor<List<String>> commandCaptor = ArgumentCaptor.forClass(List.class);
      Mockito.verify(shellProcessHandler, times(2))
          .run(
              commandCaptor.capture(),
              anyMap(),
              anyBoolean(),
              anyString(),
              any(RedactingService.RedactionTarget.class));
      List<List<String>> allCommands = commandCaptor.getAllValues();
      List<String> installCommand = allCommands.get(1);
      assertTrue(
          "helm install command should not contain --labels flag when no commonLabels present",
          !installCommand.contains("--labels"));
    } finally {
      Files.deleteIfExists(overrideFile);
      helmChartFile.delete();
    }
  }

  @Test
  public void testHelmUpgradeWithCommonLabels() throws IOException {
    // Create a temporary override file with commonLabels
    Path overrideFile = Files.createTempFile("test-overrides", ".yml");
    String yamlContent =
        "commonLabels:\n" + "  environment: staging\n" + "  version: 2.0\n" + "replicas: 5\n";
    Files.write(overrideFile, yamlContent.getBytes());

    // Create a dummy helm chart file for versions < 2.8.0.0
    File helmChartFile = new File(TMP_CHART_PATH, "yugabyte-2.7-helm-legacy.tar.gz");
    helmChartFile.getParentFile().mkdirs();
    helmChartFile.createNewFile();

    try {
      setupKubernetesManagerForHelmTests();
      // Mock helm template response (called before upgrade)
      ShellResponse templateResponse = ShellResponse.create(0, "template output");
      // Mock kubectl diff response (diff also uses KubernetesManager's execCommand)
      ShellResponse diffResponse = ShellResponse.create(0, "");
      // Mock helm upgrade response
      ShellResponse upgradeResponse = ShellResponse.create(0, "Release upgraded");
      // Mock the 5-parameter run method for helmTemplate, diff, and helmUpgrade (all in
      // KubernetesManager)
      when(shellProcessHandler.run(
              anyList(),
              anyMap(),
              anyBoolean(),
              anyString(),
              any(RedactingService.RedactionTarget.class)))
          .thenReturn(templateResponse)
          .thenReturn(diffResponse)
          .thenReturn(upgradeResponse);

      // Mock fileHelperService for template output
      Path templateOutputFile = Files.createTempFile("helm-template", ".output");
      when(fileHelperService.createTempFile("helm-template", ".output"))
          .thenReturn(templateOutputFile);

      // Mock getHelmPackagePath
      when(mockAppConfig.getString("yb.helm.packagePath")).thenReturn(TMP_CHART_PATH);
      when(mockConfGetter.getConfForScope(any(Universe.class), any())).thenReturn(300L);

      kubernetesManager.helmUpgrade(
          universe.getUniverseUUID(),
          "2.7.0.0-b1", // Use version < 2.8.0.0 to use legacy chart
          configProvider,
          "demo-universe",
          "demo-namespace",
          overrideFile.toString());

      // Verify helm upgrade was called with --labels flag
      // Note: helmTemplate, diff, and helmUpgrade all use the 5-parameter run method
      ArgumentCaptor<List<String>> commandCaptor = ArgumentCaptor.forClass(List.class);
      Mockito.verify(shellProcessHandler, times(3))
          .run(
              commandCaptor.capture(),
              anyMap(),
              anyBoolean(),
              anyString(),
              any(RedactingService.RedactionTarget.class));
      List<List<String>> allCommands = commandCaptor.getAllValues();
      // The third call should be helm upgrade (first is helm template, second is kubectl diff)
      List<String> upgradeCommand = allCommands.get(2);
      assertTrue(
          "helm upgrade command should contain --labels flag", upgradeCommand.contains("--labels"));
      int labelsIndex = upgradeCommand.indexOf("--labels");
      String labelsValue = upgradeCommand.get(labelsIndex + 1);
      assertTrue(
          "Labels should contain environment=staging", labelsValue.contains("environment=staging"));
      assertTrue("Labels should contain version=2.0", labelsValue.contains("version=2.0"));
    } finally {
      Files.deleteIfExists(overrideFile);
      helmChartFile.delete();
    }
  }

  @Test
  public void testHelmInstallWithCommonLabelsNonStringValues() throws IOException {
    // Create a temporary override file with commonLabels containing non-string values
    Path overrideFile = Files.createTempFile("test-overrides", ".yml");
    String yamlContent =
        "commonLabels:\n"
            + "  environment: production\n"
            + "  replicas: 3\n"
            + "  enabled: true\n"
            + "  priority: 1\n";
    Files.write(overrideFile, yamlContent.getBytes());

    // Create a dummy helm chart file for versions < 2.8.0.0
    File helmChartFile = new File(TMP_CHART_PATH, "yugabyte-2.7-helm-legacy.tar.gz");
    helmChartFile.getParentFile().mkdirs();
    helmChartFile.createNewFile();

    try {
      setupKubernetesManagerForHelmTests();
      // Mock helm list to return empty (no existing release)
      ShellResponse listResponse = ShellResponse.create(0, "");
      // Mock helm install response
      ShellResponse installResponse = ShellResponse.create(0, "Release installed");
      // Mock the 5-parameter run method that execCommand uses
      when(shellProcessHandler.run(
              anyList(),
              anyMap(),
              anyBoolean(),
              anyString(),
              any(RedactingService.RedactionTarget.class)))
          .thenReturn(listResponse)
          .thenReturn(installResponse);

      // Mock getHelmPackagePath
      when(mockAppConfig.getString("yb.helm.packagePath")).thenReturn(TMP_CHART_PATH);
      when(mockConfGetter.getConfForScope(any(Universe.class), any())).thenReturn(300L);

      kubernetesManager.helmInstall(
          universe.getUniverseUUID(),
          "2.7.0.0-b1", // Use version < 2.8.0.0 to use legacy chart
          configProvider,
          defaultProvider.getUuid(),
          "demo-universe",
          "demo-namespace",
          overrideFile.toString());

      // Verify helm install was called with --labels flag containing converted values
      // Note: execCommand uses the 5-parameter run method
      ArgumentCaptor<List<String>> commandCaptor = ArgumentCaptor.forClass(List.class);
      Mockito.verify(shellProcessHandler, times(2))
          .run(
              commandCaptor.capture(),
              anyMap(),
              anyBoolean(),
              anyString(),
              any(RedactingService.RedactionTarget.class));
      List<List<String>> allCommands = commandCaptor.getAllValues();
      List<String> installCommand = allCommands.get(1);
      assertTrue(
          "helm install command should contain --labels flag", installCommand.contains("--labels"));
      int labelsIndex = installCommand.indexOf("--labels");
      String labelsValue = installCommand.get(labelsIndex + 1);
      assertTrue(
          "Labels should contain environment=production",
          labelsValue.contains("environment=production"));
      assertTrue("Labels should contain replicas=3", labelsValue.contains("replicas=3"));
      assertTrue("Labels should contain enabled=true", labelsValue.contains("enabled=true"));
      assertTrue("Labels should contain priority=1", labelsValue.contains("priority=1"));
    } finally {
      Files.deleteIfExists(overrideFile);
      helmChartFile.delete();
    }
  }

  @Test
  public void testHelmInstallWithEmptyCommonLabels() throws IOException {
    // Create a temporary override file with empty commonLabels
    Path overrideFile = Files.createTempFile("test-overrides", ".yml");
    String yamlContent = "commonLabels: {}\n" + "replicas: 3\n";
    Files.write(overrideFile, yamlContent.getBytes());

    // Create a dummy helm chart file for versions < 2.8.0.0
    File helmChartFile = new File(TMP_CHART_PATH, "yugabyte-2.7-helm-legacy.tar.gz");
    helmChartFile.getParentFile().mkdirs();
    helmChartFile.createNewFile();

    try {
      setupKubernetesManagerForHelmTests();
      // Mock helm list to return empty (no existing release)
      ShellResponse listResponse = ShellResponse.create(0, "");
      // Mock helm install response
      ShellResponse installResponse = ShellResponse.create(0, "Release installed");
      // Mock the 5-parameter run method that execCommand uses
      when(shellProcessHandler.run(
              anyList(),
              anyMap(),
              anyBoolean(),
              anyString(),
              any(RedactingService.RedactionTarget.class)))
          .thenReturn(listResponse)
          .thenReturn(installResponse);

      // Mock getHelmPackagePath
      when(mockAppConfig.getString("yb.helm.packagePath")).thenReturn(TMP_CHART_PATH);
      when(mockConfGetter.getConfForScope(any(Universe.class), any())).thenReturn(300L);

      kubernetesManager.helmInstall(
          universe.getUniverseUUID(),
          "2.7.0.0-b1", // Use version < 2.8.0.0 to use legacy chart
          configProvider,
          defaultProvider.getUuid(),
          "demo-universe",
          "demo-namespace",
          overrideFile.toString());

      // Verify helm install was called without --labels flag when commonLabels is empty
      // Note: execCommand uses the 5-parameter run method
      ArgumentCaptor<List<String>> commandCaptor = ArgumentCaptor.forClass(List.class);
      Mockito.verify(shellProcessHandler, times(2))
          .run(
              commandCaptor.capture(),
              anyMap(),
              anyBoolean(),
              anyString(),
              any(RedactingService.RedactionTarget.class));
      List<List<String>> allCommands = commandCaptor.getAllValues();
      List<String> installCommand = allCommands.get(1);
      assertTrue(
          "helm install command should not contain --labels flag when commonLabels is empty",
          !installCommand.contains("--labels"));
    } finally {
      Files.deleteIfExists(overrideFile);
      helmChartFile.delete();
    }
  }
}
