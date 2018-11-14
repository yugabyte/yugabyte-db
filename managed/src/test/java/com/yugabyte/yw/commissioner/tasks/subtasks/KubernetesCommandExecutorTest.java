// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.KubernetesManager;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.yaml.snakeyaml.Yaml;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.test.Helpers;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
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
  Region defaultRegion;
  AvailabilityZone defaultAZ;
  // TODO: when trying to fetch the cluster UUID directly, we get:
  // javax.persistence.EntityNotFoundException: Bean not found during lazy load or refresh
  //
  // This is mostly because we cache the defaultUniverse, but actually update the universe itself
  // through calls to saveDetails, which OBVIOUSLY will not be reflected into the defaultUniverse
  // field in the test...
  UUID hackPlacementUUID;
  UniverseDefinitionTaskParams.UserIntent defaultUserIntent;
  InstanceType instanceType;
  String ybSoftwareVersion = "1.0.0";
  int numNodes = 3;

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
    defaultRegion = Region.create(defaultProvider, "region-1", "PlacementRegion 1", "default-image");
    defaultAZ = AvailabilityZone.create(defaultRegion, "az-1", "PlacementAZ 1", "subnet-1");
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    defaultUniverse = updateUniverseDetails("small");
  }

  private Universe updateUniverseDetails(String instanceTypeCode) {
    instanceType = InstanceType.upsert(defaultProvider.code, instanceTypeCode,
        10, 5.5, new InstanceType.InstanceTypeDetails());
    defaultUserIntent = getTestUserIntent(
        defaultRegion, defaultProvider, instanceType, numNodes);
    defaultUserIntent.replicationFactor = 3;
    defaultUserIntent.masterGFlags = new HashMap<>();
    defaultUserIntent.tserverGFlags = new HashMap<>();
    defaultUserIntent.universeName = "demo-universe";
    defaultUserIntent.ybSoftwareVersion = ybSoftwareVersion;
    Universe u = Universe.saveDetails(defaultUniverse.universeUUID,
        ApiUtils.mockUniverseUpdater(defaultUserIntent, "host", true));
    hackPlacementUUID = u.getUniverseDetails().getPrimaryCluster().uuid;
    return u;
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
    kubernetesCommandExecutor.run();
    verify(kubernetesManager, times(1)).helmInit(defaultProvider.uuid);
  }

  private Map<String, Object> getExpectedOverrides(boolean exposeAll) {
    Yaml yaml = new Yaml();
    Map<String, Object> expectedOverrides = new HashMap<>();
    if (exposeAll) {
      expectedOverrides = (HashMap<String, Object>) yaml.load(
          provideApplication().resourceAsStream("k8s-expose-all.yml")
      );
    }
    double burstVal = 1.2;
    Map<String, String> config = defaultProvider.getConfig();

    Map<String, Object> diskSpecs = new HashMap<>();
    if (defaultUserIntent.deviceInfo != null) {
      if (defaultUserIntent.deviceInfo.numVolumes != null) {
        diskSpecs.put("count", defaultUserIntent.deviceInfo.numVolumes);
      }
      if (defaultUserIntent.deviceInfo.volumeSize != null) {
        diskSpecs.put("storage", String.format("%dGi", defaultUserIntent.deviceInfo.volumeSize));
      }
      if (defaultUserIntent.deviceInfo.storageClass != null) {
        diskSpecs.put("storageClass", defaultUserIntent.deviceInfo.storageClass);
      }
      if (!diskSpecs.isEmpty()) {
        expectedOverrides.put("persistentVolume", diskSpecs);
      }
    }
    Map<String, Object> resourceOverrides = new HashMap();

    Map<String, Object> tserverResource = new HashMap<>();
    Map<String, Object> tserverLimit = new HashMap<>();
    tserverResource.put("cpu", instanceType.numCores);
    tserverResource.put("memory", String.format("%.2fGi", instanceType.memSizeGB));
    tserverLimit.put("cpu", instanceType.numCores * burstVal);
    resourceOverrides.put("tserver", ImmutableMap.of("requests", tserverResource, "limits", tserverLimit));

    if (!instanceType.getInstanceTypeCode().equals("xsmall") &&
        !instanceType.getInstanceTypeCode().equals("dev")) {
      Map<String, Object> masterResource = new HashMap<>();
      Map<String, Object> masterLimit = new HashMap<>();
      masterResource.put("cpu", 2);
      masterResource.put("memory", "4Gi");
      masterLimit.put("cpu", 2 * burstVal);
      resourceOverrides.put("master", ImmutableMap.of("requests", masterResource, "limits", masterLimit));
    }

    expectedOverrides.put("resource", resourceOverrides);

    expectedOverrides.put("Image", ImmutableMap.of("tag", ybSoftwareVersion));
    expectedOverrides.put("replicas", ImmutableMap.of("tserver", numNodes,
        "master", defaultUserIntent.replicationFactor));

    // All flags as overrides.
    Map<String, Object> gflagOverrides = new HashMap<>();
    // Master flags.
    Map<String, Object> masterOverrides = new HashMap<String, Object>(defaultUserIntent.masterGFlags);
    masterOverrides.put("placement_cloud", defaultProvider.code);
    masterOverrides.put("placement_region", defaultRegion.code);
    masterOverrides.put("placement_zone", defaultAZ.code);
    // masterOverrides.put("placement_uuid", defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid);
    masterOverrides.put("placement_uuid", hackPlacementUUID.toString());
    gflagOverrides.put("master", masterOverrides);

    // Tserver flags.
    Map<String, Object> tserverOverrides = new HashMap<String, Object>(defaultUserIntent.tserverGFlags);
    tserverOverrides.put("placement_cloud", defaultProvider.code);
    tserverOverrides.put("placement_region", defaultRegion.code);
    tserverOverrides.put("placement_zone", defaultAZ.code);
    // tserverOverrides.put("placement_uuid", defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid);
    tserverOverrides.put("placement_uuid", hackPlacementUUID.toString());
    gflagOverrides.put("tserver", tserverOverrides);
    // Put all the flags together.
    expectedOverrides.put("gflags", gflagOverrides);

    Map<String, Object> annotations = new HashMap<String, Object>();
    if (config.containsKey("KUBECONFIG_ANNOTATIONS")) {
      annotations =(HashMap<String, Object>) yaml.load(
          config.get("KUBECONFIG_ANNOTATIONS"));
      expectedOverrides.putAll(annotations);
    }

    return expectedOverrides;
  }

  @Test
  public void testHelmInstall() throws IOException {
    assertEquals(hackPlacementUUID, defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid);
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(KubernetesCommandExecutor.CommandType.HELM_INSTALL);
    kubernetesCommandExecutor.run();
    assertEquals(hackPlacementUUID, defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid);

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    verify(kubernetesManager, times(1))
        .helmInstall(expectedProviderUUID.capture(), expectedNodePrefix.capture(),
            expectedOverrideFile.capture());
    assertEquals(hackPlacementUUID, defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid);
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    String overrideFileRegex = "(.*)" + defaultUniverse.universeUUID + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    // TODO implement exposeAll false case
    assertEquals(hackPlacementUUID, defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid);
    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallWithGflags() throws IOException {
    defaultUserIntent.masterGFlags = ImmutableMap.of("yb-master-flag", "demo-flag");
    defaultUserIntent.tserverGFlags = ImmutableMap.of("yb-tserver-flag", "demo-flag");
    defaultUserIntent.ybSoftwareVersion = ybSoftwareVersion;
    Universe u = Universe.saveDetails(defaultUniverse.universeUUID,
        ApiUtils.mockUniverseUpdater(defaultUserIntent, "host", true));
    hackPlacementUUID = u.getUniverseDetails().getPrimaryCluster().uuid;

    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(KubernetesCommandExecutor.CommandType.HELM_INSTALL);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    verify(kubernetesManager, times(1))
        .helmInstall(expectedProviderUUID.capture(), expectedNodePrefix.capture(),
            expectedOverrideFile.capture());
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    String overrideFileRegex = "(.*)" + defaultUniverse.universeUUID + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    // TODO implement exposeAll false case
    assertEquals(getExpectedOverrides(true), overrides);
  }

  private void testHelmInstallForInstanceType(String instanceType) throws IOException {
    defaultUniverse = updateUniverseDetails(instanceType);
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(KubernetesCommandExecutor.CommandType.HELM_INSTALL);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    verify(kubernetesManager, times(1))
        .helmInstall(expectedProviderUUID.capture(), expectedNodePrefix.capture(),
            expectedOverrideFile.capture());
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    String overrideFileRegex = "(.*)" + defaultUniverse.universeUUID + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);
    Map<String, Object> resourceOverrides = (Map<String, Object>) overrides.get("resource");
    assertFalse(resourceOverrides.containsKey("master"));
    assertTrue(resourceOverrides.containsKey("tserver"));
    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallForXSmallInstance() throws IOException {
    testHelmInstallForInstanceType("xsmall");
  }

  @Test
  public void testHelmInstallForDevInstance() throws IOException {
    testHelmInstallForInstanceType("dev");
  }

  @Test
  public void testHelmInstallForAnnotations() throws IOException {
    Map<String, String> defaultAnnotations = new HashMap<String, String>();
    defaultAnnotations.put("KUBECONFIG_ANNOTATIONS", "annotations:\n  master:\n    loadbalancer:\n      annotation-1: foo");
    defaultProvider.setConfig(defaultAnnotations);
    defaultProvider.save();
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(KubernetesCommandExecutor.CommandType.HELM_INSTALL);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    verify(kubernetesManager, times(1))
        .helmInstall(expectedProviderUUID.capture(), expectedNodePrefix.capture(),
            expectedOverrideFile.capture());
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    String overrideFileRegex = "(.*)" + defaultUniverse.universeUUID + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    // TODO implement exposeAll false case
    assertEquals(getExpectedOverrides(true), overrides);
  }


  @Test
  public void testHelmInstallWithStorageClass() throws IOException {
    defaultUserIntent.deviceInfo = new DeviceInfo();
    defaultUserIntent.deviceInfo.storageClass = "foo";
    Universe u = Universe.saveDetails(defaultUniverse.universeUUID,
        ApiUtils.mockUniverseUpdater(defaultUserIntent, "host", true));
    hackPlacementUUID = u.getUniverseDetails().getPrimaryCluster().uuid;
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(KubernetesCommandExecutor.CommandType.HELM_INSTALL);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    verify(kubernetesManager, times(1))
        .helmInstall(expectedProviderUUID.capture(), expectedNodePrefix.capture(),
            expectedOverrideFile.capture());
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    String overrideFileRegex = "(.*)" + defaultUniverse.universeUUID + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    // TODO implement exposeAll false case
    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmDelete() {
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(KubernetesCommandExecutor.CommandType.HELM_DELETE);
    kubernetesCommandExecutor.run();
    verify(kubernetesManager, times(1))
        .helmDelete(defaultProvider.uuid, defaultUniverse.getUniverseDetails().nodePrefix);
  }

  @Test
  public void testVolumeDelete() {
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(KubernetesCommandExecutor.CommandType.VOLUME_DELETE);
    kubernetesCommandExecutor.run();
    verify(kubernetesManager, times(1))
        .deleteStorage(defaultProvider.uuid, defaultUniverse.getUniverseDetails().nodePrefix);
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
    assertEquals(3, defaultUniverse.getNodes().size());
    kubernetesCommandExecutor.run();
    verify(kubernetesManager, times(1))
        .getPodInfos(defaultProvider.uuid, defaultUniverse.getUniverseDetails().nodePrefix);
    defaultUniverse = Universe.get(defaultUniverse.universeUUID);
    assertEquals(6, defaultUniverse.getNodes().size());
  }
}
