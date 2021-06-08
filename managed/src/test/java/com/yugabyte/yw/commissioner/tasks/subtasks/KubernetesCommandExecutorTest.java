// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.*;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.pac4j.play.CallbackController;
import org.pac4j.play.store.PlayCacheSessionStore;
import org.pac4j.play.store.PlaySessionStore;
import org.yaml.snakeyaml.Yaml;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.test.Helpers;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ExposingServiceState;
import static org.junit.Assert.*;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.eq;
import static org.mockito.Mockito.*;
import static play.inject.Bindings.bind;

public class KubernetesCommandExecutorTest extends SubTaskBaseTest {
  KubernetesManager kubernetesManager;
  Provider defaultProvider;
  Universe defaultUniverse;
  Region defaultRegion;
  AvailabilityZone defaultAZ;
  CertificateInfo defaultCert;
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
  String namespace = "demo-ns";
  Map<String, String> config = new HashMap<String, String>();

  protected CallbackController mockCallbackController;
  protected PlayCacheSessionStore mockSessionStore;

  @Override
  protected Application provideApplication() {
    kubernetesManager = mock(KubernetesManager.class);
    mockCallbackController = mock(CallbackController.class);
    mockSessionStore = mock(PlayCacheSessionStore.class);
    return new GuiceApplicationBuilder()
        .configure((Map) Helpers.inMemoryDatabase())
        .overrides(bind(KubernetesManager.class).toInstance(kubernetesManager))
        .overrides(bind(CallbackController.class).toInstance(mockCallbackController))
        .overrides(bind(PlaySessionStore.class).toInstance(mockSessionStore))
        .build();
  }

  @Before
  public void setUp() {
    super.setUp();
    // TODO(bhavin192): shouldn't this be a Kubernetes provider?
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
    defaultRegion =
        Region.create(defaultProvider, "region-1", "PlacementRegion 1", "default-image");
    defaultAZ = AvailabilityZone.create(defaultRegion, "az-1", "PlacementAZ 1", "subnet-1");
    config.put("KUBECONFIG", "test");
    defaultAZ.updateConfig(config);
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    defaultUniverse = updateUniverseDetails("small");
    defaultCert =
        CertificateInfo.get(
            CertificateHelper.createRootCA(
                defaultUniverse.getUniverseDetails().nodePrefix,
                defaultProvider.customerUUID,
                "/tmp/certs"));
    defaultUniverse.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File("/tmp/certs"));
  }

  private Universe updateUniverseDetails(String instanceTypeCode) {
    instanceType =
        InstanceType.upsert(
            defaultProvider.uuid,
            instanceTypeCode,
            10,
            5.5,
            new InstanceType.InstanceTypeDetails());
    defaultUserIntent = getTestUserIntent(defaultRegion, defaultProvider, instanceType, numNodes);
    defaultUserIntent.replicationFactor = 3;
    defaultUserIntent.masterGFlags = new HashMap<>();
    defaultUserIntent.tserverGFlags = new HashMap<>();
    defaultUserIntent.universeName = "demo-universe";
    defaultUserIntent.ybSoftwareVersion = ybSoftwareVersion;
    defaultUserIntent.enableYSQL = true;
    Universe u =
        Universe.saveDetails(
            defaultUniverse.universeUUID,
            ApiUtils.mockUniverseUpdater(defaultUserIntent, "host", true));
    hackPlacementUUID = u.getUniverseDetails().getPrimaryCluster().uuid;
    return u;
  }

  private KubernetesCommandExecutor createExecutor(
      KubernetesCommandExecutor.CommandType commandType, boolean setNamespace) {
    KubernetesCommandExecutor kubernetesCommandExecutor =
        AbstractTaskBase.createTask(KubernetesCommandExecutor.class);
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    params.providerUUID = defaultProvider.uuid;
    params.commandType = commandType;
    params.config = config;
    params.nodePrefix = defaultUniverse.getUniverseDetails().nodePrefix;
    params.universeUUID = defaultUniverse.universeUUID;
    if (setNamespace) {
      params.namespace = namespace;
    }
    kubernetesCommandExecutor.initialize(params);
    return kubernetesCommandExecutor;
  }

  private KubernetesCommandExecutor createExecutor(
      KubernetesCommandExecutor.CommandType commandType, PlacementInfo placementInfo) {
    KubernetesCommandExecutor kubernetesCommandExecutor =
        AbstractTaskBase.createTask(KubernetesCommandExecutor.class);
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    params.providerUUID = defaultProvider.uuid;
    params.commandType = commandType;
    params.nodePrefix = defaultUniverse.getUniverseDetails().nodePrefix;
    params.universeUUID = defaultUniverse.universeUUID;
    params.config = config;
    params.placementInfo = placementInfo;
    kubernetesCommandExecutor.initialize(params);
    return kubernetesCommandExecutor;
  }

  private Map<String, Object> getExpectedOverrides(boolean exposeAll) {
    Yaml yaml = new Yaml();
    Map<String, Object> expectedOverrides = new HashMap<>();
    if (exposeAll) {
      expectedOverrides =
          (HashMap<String, Object>)
              yaml.load(provideApplication().resourceAsStream("k8s-expose-all.yml"));
    }
    double burstVal = 1.2;
    Map<String, String> config = defaultProvider.getConfig();

    Map<String, Object> storageOverrides =
        (HashMap) expectedOverrides.getOrDefault("storage", new HashMap<>());
    if (defaultUserIntent.deviceInfo != null) {
      Map<String, Object> tserverDiskSpecs =
          (HashMap) storageOverrides.getOrDefault("tserver", new HashMap<>());
      Map<String, Object> masterDiskSpecs =
          (HashMap) storageOverrides.getOrDefault("master", new HashMap<>());

      if (defaultUserIntent.deviceInfo.numVolumes != null) {
        tserverDiskSpecs.put("count", defaultUserIntent.deviceInfo.numVolumes);
      }
      if (defaultUserIntent.deviceInfo.volumeSize != null) {
        tserverDiskSpecs.put(
            "size", String.format("%dGi", defaultUserIntent.deviceInfo.volumeSize));
      }
      if (defaultUserIntent.deviceInfo.storageClass != null) {
        tserverDiskSpecs.put("storageClass", defaultUserIntent.deviceInfo.storageClass);
        masterDiskSpecs.put("storageClass", defaultUserIntent.deviceInfo.storageClass);
      }
      storageOverrides.put("tserver", tserverDiskSpecs);
      storageOverrides.put("master", masterDiskSpecs);
    }

    expectedOverrides.put(
        "replicas",
        ImmutableMap.of("tserver", numNodes, "master", defaultUserIntent.replicationFactor));

    Map<String, Object> resourceOverrides = new HashMap();

    Map<String, Object> tserverResource = new HashMap<>();
    Map<String, Object> tserverLimit = new HashMap<>();
    tserverResource.put("cpu", instanceType.numCores);
    tserverResource.put("memory", String.format("%.2fGi", instanceType.memSizeGB));
    tserverLimit.put("cpu", instanceType.numCores * burstVal);
    tserverLimit.put("memory", String.format("%.2fGi", instanceType.memSizeGB));
    resourceOverrides.put(
        "tserver", ImmutableMap.of("requests", tserverResource, "limits", tserverLimit));

    Map<String, Object> masterResource = new HashMap<>();
    Map<String, Object> masterLimit = new HashMap<>();

    if (!instanceType.getInstanceTypeCode().equals("xsmall")
        && !instanceType.getInstanceTypeCode().equals("dev")) {
      masterResource.put("cpu", 2);
      masterResource.put("memory", "4Gi");
      masterLimit.put("cpu", 2 * burstVal);
      masterLimit.put("memory", "4Gi");
      resourceOverrides.put(
          "master", ImmutableMap.of("requests", masterResource, "limits", masterLimit));
    }

    if (instanceType.getInstanceTypeCode().equals("dev")) {
      masterResource.put("cpu", 0.5);
      masterResource.put("memory", "0.5Gi");
      masterLimit.put("cpu", 0.5);
      masterLimit.put("memory", "0.5Gi");
      resourceOverrides.put(
          "master", ImmutableMap.of("requests", masterResource, "limits", masterLimit));
    }

    expectedOverrides.put("resource", resourceOverrides);

    expectedOverrides.put("Image", ImmutableMap.of("tag", ybSoftwareVersion));
    if (defaultUserIntent.enableNodeToNodeEncrypt || defaultUserIntent.enableClientToNodeEncrypt) {
      Map<String, Object> tlsInfo = new HashMap<>();
      tlsInfo.put("enabled", true);
      tlsInfo.put("nodeToNode", defaultUserIntent.enableNodeToNodeEncrypt);
      tlsInfo.put("clientToServer", defaultUserIntent.enableClientToNodeEncrypt);
      tlsInfo.put("insecure", true);
      Map<String, Object> rootCA = new HashMap<>();
      rootCA.put("cert", CertificateHelper.getCertPEM(defaultCert));
      rootCA.put("key", CertificateHelper.getKeyPEM(defaultCert));
      tlsInfo.put("rootCA", rootCA);
      expectedOverrides.put("tls", tlsInfo);
    }
    if (defaultUserIntent.enableIPV6) {
      expectedOverrides.put("ip_version_support", "v6_only");
    }

    // For all tests but 1, value should default to true.
    if (defaultUserIntent.enableExposingService == ExposingServiceState.UNEXPOSED) {
      expectedOverrides.put("enableLoadBalancer", false);
    } else {
      expectedOverrides.put("enableLoadBalancer", true);
    }

    Map<String, Object> partition = new HashMap<>();
    partition.put("tserver", 0);
    partition.put("master", 0);
    expectedOverrides.put("partition", partition);

    // All flags as overrides.
    Map<String, Object> gflagOverrides = new HashMap<>();
    // Master flags.
    Map<String, Object> masterOverrides =
        new HashMap<String, Object>(defaultUserIntent.masterGFlags);
    masterOverrides.put("placement_cloud", defaultProvider.code);
    masterOverrides.put("placement_region", defaultRegion.code);
    masterOverrides.put("placement_zone", defaultAZ.code);
    // masterOverrides.put("placement_uuid",
    // defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid);
    masterOverrides.put("placement_uuid", hackPlacementUUID.toString());
    gflagOverrides.put("master", masterOverrides);

    // Tserver flags.
    Map<String, Object> tserverOverrides =
        new HashMap<String, Object>(defaultUserIntent.tserverGFlags);
    tserverOverrides.put("placement_cloud", defaultProvider.code);
    tserverOverrides.put("placement_region", defaultRegion.code);
    tserverOverrides.put("placement_zone", defaultAZ.code);
    // tserverOverrides.put("placement_uuid",
    // defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid);
    tserverOverrides.put("placement_uuid", hackPlacementUUID.toString());

    gflagOverrides.put("tserver", tserverOverrides);
    // Put all the flags together.
    expectedOverrides.put("gflags", gflagOverrides);

    Map<String, String> azConfig = defaultAZ.getConfig();
    Map<String, String> regionConfig = defaultRegion.getConfig();

    Map<String, Object> annotations = new HashMap<String, Object>();
    String overridesYAML = null;
    if (!azConfig.containsKey("OVERRIDES")) {
      if (!regionConfig.containsKey("OVERRIDES")) {
        if (config.containsKey("OVERRIDES")) {
          overridesYAML = config.get("OVERRIDES");
        }
      } else {
        overridesYAML = regionConfig.get("OVERRIDES");
      }
    } else {
      overridesYAML = azConfig.get("OVERRIDES");
    }

    if (overridesYAML != null) {
      annotations = (HashMap<String, Object>) yaml.load(overridesYAML);
      if (annotations != null) {
        expectedOverrides.putAll(annotations);
      }
    }
    expectedOverrides.put("disableYsql", !defaultUserIntent.enableYSQL);
    Map<String, String> universeConfig = defaultUniverse.getConfig();
    boolean helmLegacy =
        Universe.HelmLegacy.valueOf(universeConfig.get(Universe.HELM2_LEGACY))
            == Universe.HelmLegacy.V2TO3;
    if (helmLegacy) {
      expectedOverrides.put("helm2Legacy", helmLegacy);
    }

    return expectedOverrides;
  }

  @Test
  public void testHelmInstall() throws IOException {
    assertEquals(hackPlacementUUID, defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid);
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();
    assertEquals(hackPlacementUUID, defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid);

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<HashMap> expectedConfig = ArgumentCaptor.forClass(HashMap.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(hackPlacementUUID, defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid);
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());
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
  public void testHelmInstallIPV6() throws IOException {
    defaultUserIntent.enableIPV6 = true;
    Universe u =
        Universe.saveDetails(
            defaultUniverse.universeUUID,
            ApiUtils.mockUniverseUpdater(defaultUserIntent, "host", true));
    hackPlacementUUID = u.getUniverseDetails().getPrimaryCluster().uuid;

    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<HashMap> expectedConfig = ArgumentCaptor.forClass(HashMap.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.universeUUID + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallWithoutLoadbalancer() throws IOException {
    defaultUserIntent.enableExposingService = ExposingServiceState.UNEXPOSED;
    Universe u =
        Universe.saveDetails(
            defaultUniverse.universeUUID,
            ApiUtils.mockUniverseUpdater(defaultUserIntent, "host", true));
    hackPlacementUUID = u.getUniverseDetails().getPrimaryCluster().uuid;

    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<HashMap> expectedConfig = ArgumentCaptor.forClass(HashMap.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());
    String overrideFileRegex = "(.*)" + defaultUniverse.universeUUID + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallWithGflags() throws IOException {
    defaultUserIntent.masterGFlags = ImmutableMap.of("yb-master-flag", "demo-flag");
    defaultUserIntent.tserverGFlags = ImmutableMap.of("yb-tserver-flag", "demo-flag");
    defaultUserIntent.ybSoftwareVersion = ybSoftwareVersion;
    Universe u =
        Universe.saveDetails(
            defaultUniverse.universeUUID,
            ApiUtils.mockUniverseUpdater(defaultUserIntent, "host", true));
    hackPlacementUUID = u.getUniverseDetails().getPrimaryCluster().uuid;

    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<HashMap> expectedConfig = ArgumentCaptor.forClass(HashMap.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.universeUUID + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    // TODO implement exposeAll false case
    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallWithTLS() throws IOException {
    defaultUserIntent.masterGFlags = ImmutableMap.of("yb-master-flag", "demo-flag");
    defaultUserIntent.tserverGFlags = ImmutableMap.of("yb-tserver-flag", "demo-flag");
    defaultUserIntent.ybSoftwareVersion = ybSoftwareVersion;
    defaultUserIntent.enableNodeToNodeEncrypt = true;
    defaultUserIntent.enableClientToNodeEncrypt = true;
    Universe u =
        Universe.saveDetails(
            defaultUniverse.universeUUID,
            ApiUtils.mockUniverseUpdater(defaultUserIntent, "host", true));
    hackPlacementUUID = u.getUniverseDetails().getPrimaryCluster().uuid;
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.taskParams().rootCA = defaultCert.uuid;
    kubernetesCommandExecutor.run();

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<HashMap> expectedConfig = ArgumentCaptor.forClass(HashMap.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.universeUUID + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    // TODO implement exposeAll false case
    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallWithTLSNodeToNode() throws IOException {
    defaultUserIntent.masterGFlags = ImmutableMap.of("yb-master-flag", "demo-flag");
    defaultUserIntent.tserverGFlags = ImmutableMap.of("yb-tserver-flag", "demo-flag");
    defaultUserIntent.ybSoftwareVersion = ybSoftwareVersion;
    defaultUserIntent.enableNodeToNodeEncrypt = true;
    defaultUserIntent.enableClientToNodeEncrypt = false;
    Universe u =
        Universe.saveDetails(
            defaultUniverse.universeUUID,
            ApiUtils.mockUniverseUpdater(defaultUserIntent, "host", true));
    hackPlacementUUID = u.getUniverseDetails().getPrimaryCluster().uuid;
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.taskParams().rootCA = defaultCert.uuid;
    kubernetesCommandExecutor.run();

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<HashMap> expectedConfig = ArgumentCaptor.forClass(HashMap.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.universeUUID + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    // TODO implement exposeAll false case
    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallWithTLSClientToServer() throws IOException {
    defaultUserIntent.masterGFlags = ImmutableMap.of("yb-master-flag", "demo-flag");
    defaultUserIntent.tserverGFlags = ImmutableMap.of("yb-tserver-flag", "demo-flag");
    defaultUserIntent.ybSoftwareVersion = ybSoftwareVersion;
    defaultUserIntent.enableNodeToNodeEncrypt = false;
    defaultUserIntent.enableClientToNodeEncrypt = true;
    Universe u =
        Universe.saveDetails(
            defaultUniverse.universeUUID,
            ApiUtils.mockUniverseUpdater(defaultUserIntent, "host", true));
    hackPlacementUUID = u.getUniverseDetails().getPrimaryCluster().uuid;
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.taskParams().rootCA = defaultCert.uuid;
    kubernetesCommandExecutor.run();

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<HashMap> expectedConfig = ArgumentCaptor.forClass(HashMap.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

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
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<HashMap> expectedConfig = ArgumentCaptor.forClass(HashMap.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.universeUUID + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);
    Map<String, Object> resourceOverrides = (Map<String, Object>) overrides.get("resource");
    if (instanceType.equals("dev")) {
      assertTrue(resourceOverrides.containsKey("master"));
    } else {
      assertFalse(resourceOverrides.containsKey("master"));
    }

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
    defaultAnnotations.put(
        "OVERRIDES",
        "serviceEndpoints:\n  - name: yb-master-service\n    type: LoadBalancer\n    app: yb-master\n    annotations:\n      annotation-1: foo\n    ports:\n      ui: 7000\n\n  - name: yb-tserver-service\n    type: LoadBalancer\n    app: yb-tserver\n    ports:\n      ycql-port: 9042\n      yedis-port: 6379");
    defaultProvider.setConfig(defaultAnnotations);
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<HashMap> expectedConfig = ArgumentCaptor.forClass(HashMap.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.universeUUID + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    // TODO implement exposeAll false case
    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallForAnnotationsRegion() throws IOException {
    Map<String, String> defaultAnnotations = new HashMap<String, String>();
    defaultAnnotations.put(
        "OVERRIDES",
        "serviceEndpoints:\n  - name: yb-master-service\n    type: LoadBalancer\n    app: yb-master\n    annotations:\n      annotation-1: bar\n    ports:\n      ui: 7000\n\n  - name: yb-tserver-service\n    type: LoadBalancer\n    app: yb-tserver\n    ports:\n      ycql-port: 9042\n      yedis-port: 6379");
    defaultRegion.setConfig(defaultAnnotations);
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<HashMap> expectedConfig = ArgumentCaptor.forClass(HashMap.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.universeUUID + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    // TODO implement exposeAll false case
    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallForAnnotationsAZ() throws IOException {
    Map<String, String> defaultAnnotations = new HashMap<String, String>();
    defaultAnnotations.put(
        "OVERRIDES",
        "serviceEndpoints:\n  - name: yb-master-service\n    type: LoadBalancer\n    app: yb-master\n    annotations:\n      annotation-1: bar\n    ports:\n      ui: 7000\n\n  - name: yb-tserver-service\n    type: LoadBalancer\n    app: yb-tserver\n    ports:\n      ycql-port: 9042\n      yedis-port: 6379");
    defaultAZ.updateConfig(defaultAnnotations);
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<HashMap> expectedConfig = ArgumentCaptor.forClass(HashMap.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.universeUUID + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    // TODO implement exposeAll false case
    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallForAnnotationsPrecendence() throws IOException {
    Map<String, String> defaultAnnotations = new HashMap<String, String>();
    defaultAnnotations.put("OVERRIDES", "foo: bar");
    defaultProvider.setConfig(defaultAnnotations);
    defaultAnnotations.put("OVERRIDES", "bar: foo");
    defaultAZ.updateConfig(defaultAnnotations);

    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<HashMap> expectedConfig = ArgumentCaptor.forClass(HashMap.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.universeUUID + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);
    assertEquals(overrides.get("bar"), "foo");
    // TODO implement exposeAll false case
    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallResourceOverrideMerge() throws IOException {
    Map<String, String> defaultAnnotations = new HashMap<String, String>();
    defaultAnnotations.put("OVERRIDES", "resource:\n  master:\n    limits:\n      cpu: 650m");
    defaultAZ.updateConfig(defaultAnnotations);
    defaultUniverse = updateUniverseDetails("dev");
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<HashMap> expectedConfig = ArgumentCaptor.forClass(HashMap.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.universeUUID + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    Map<String, Object> expectedOverrides = getExpectedOverrides(true);
    Map<String, Object> resourceOverrides = new HashMap<>();
    Map<String, Object> tserverResource = new HashMap<>();
    Map<String, Object> tserverLimit = new HashMap<>();
    Map<String, Object> masterResource = new HashMap<>();
    Map<String, Object> masterLimit = new HashMap<>();
    double burstVal = 1.2;

    tserverResource.put("cpu", instanceType.numCores);
    tserverResource.put("memory", String.format("%.2fGi", instanceType.memSizeGB));
    tserverLimit.put("cpu", instanceType.numCores * burstVal);
    tserverLimit.put("memory", String.format("%.2fGi", instanceType.memSizeGB));

    masterResource.put("cpu", 0.5);
    masterResource.put("memory", "0.5Gi");
    masterLimit.put("cpu", "650m");
    masterLimit.put("memory", "0.5Gi");

    resourceOverrides.put(
        "master", ImmutableMap.of("requests", masterResource, "limits", masterLimit));
    resourceOverrides.put(
        "tserver",
        ImmutableMap.of(
            "requests", tserverResource,
            "limits", tserverLimit));

    expectedOverrides.put("resource", resourceOverrides);

    // TODO implement exposeAll false case
    assertEquals(expectedOverrides, overrides);
  }

  @Test
  public void testHelmInstallWithStorageClass() throws IOException {
    defaultUserIntent.deviceInfo = new DeviceInfo();
    defaultUserIntent.deviceInfo.storageClass = "foo";
    Universe u =
        Universe.saveDetails(
            defaultUniverse.universeUUID,
            ApiUtils.mockUniverseUpdater(defaultUserIntent, "host", true));
    hackPlacementUUID = u.getUniverseDetails().getPrimaryCluster().uuid;
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<HashMap> expectedConfig = ArgumentCaptor.forClass(HashMap.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

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
        createExecutor(KubernetesCommandExecutor.CommandType.HELM_DELETE, /* set namespace */ true);
    kubernetesCommandExecutor.run();
    verify(kubernetesManager, times(1))
        .helmDelete(config, defaultUniverse.getUniverseDetails().nodePrefix, namespace);
  }

  @Test
  public void testVolumeDelete() {
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.VOLUME_DELETE, /* set namespace */ true);
    kubernetesCommandExecutor.run();
    verify(kubernetesManager, times(1))
        .deleteStorage(config, defaultUniverse.getUniverseDetails().nodePrefix, namespace);
  }

  @Test
  public void testNamespaceDelete() {
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.NAMESPACE_DELETE, /* set namespace */ true);
    kubernetesCommandExecutor.run();
    verify(kubernetesManager, times(1)).deleteNamespace(config, namespace);
  }

  @Test
  public void testPodInfo() {
    testPodInfoBase(false);
  }

  @Test
  public void testPodInfoWithNamespace() {
    testPodInfoBase(true);
  }

  private void testPodInfoBase(boolean setNamespace) {
    String nodePrefix = defaultUniverse.getUniverseDetails().nodePrefix;
    String namespace = nodePrefix;
    Map<String, String> azConfig = new HashMap();
    azConfig.putAll(config);

    if (setNamespace) {
      namespace = "test-ns";
      azConfig.put("KUBECONFIG", "test-kc");
      azConfig.put("KUBENAMESPACE", namespace);
    }
    defaultAZ.updateConfig(azConfig);

    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\","
            + " \"podIP\": \"123.456.78.90\"}, \"spec\": {\"hostname\": \"yb-master-0\"},"
            + " \"metadata\": {\"namespace\": \""
            + namespace
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"123.456.78.91\"}, \"spec\": {\"hostname\": \"yb-tserver-0\"},"
            + " \"metadata\": {\"namespace\": \""
            + namespace
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"123.456.78.92\"}, \"spec\": {\"hostname\": \"yb-master-1\"},"
            + " \"metadata\": {\"namespace\": \""
            + namespace
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\","
            + " \"podIP\": \"123.456.78.93\"}, \"spec\": {\"hostname\": \"yb-tserver-1\"},"
            + " \"metadata\": {\"namespace\": \""
            + namespace
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"123.456.78.94\"}, \"spec\": {\"hostname\": \"yb-master-2\"},"
            + " \"metadata\": {\"namespace\": \""
            + namespace
            + "\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"123.456.78.95\"}, \"spec\": {\"hostname\": \"yb-tserver-2\"},"
            + " \"metadata\": {\"namespace\": \""
            + namespace
            + "\"}}]}";
    when(kubernetesManager.getPodInfos(any(), any(), any())).thenReturn(shellResponse);
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.POD_INFO,
            defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo);
    assertEquals(3, defaultUniverse.getNodes().size());
    kubernetesCommandExecutor.run();
    verify(kubernetesManager, times(1)).getPodInfos(azConfig, nodePrefix, namespace);
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.universeUUID);
    ImmutableList<String> pods =
        ImmutableList.of(
            "yb-master-0",
            "yb-master-1",
            "yb-master-2",
            "yb-tserver-0",
            "yb-tserver-1",
            "yb-tserver-2");
    for (String podName : pods) {
      NodeDetails node = defaultUniverse.getNode(podName);
      assertNotNull(node);
      String serviceName = podName.contains("master") ? "yb-masters" : "yb-tservers";
      assertTrue(podName.contains("master") ? node.isMaster : node.isTserver);
      assertEquals(
          node.cloudInfo.private_ip,
          String.format("%s.%s.%s.%s", podName, serviceName, namespace, "svc.cluster.local"));
    }
  }

  @Test
  public void testPodInfoMultiAZ() {
    testPodInfoMultiAZBase(false);
  }

  @Test
  public void testPodInfoMultiAZWithNamespace() {
    testPodInfoMultiAZBase(true);
  }

  private void testPodInfoMultiAZBase(boolean setNamespace) {
    Region r1 = Region.create(defaultProvider, "region-1", "region-1", "yb-image-1");
    Region r2 = Region.create(defaultProvider, "region-2", "region-2", "yb-image-1");
    AvailabilityZone az1 = AvailabilityZone.create(r1, "az-" + 1, "az-" + 1, "subnet-" + 1);
    AvailabilityZone az2 = AvailabilityZone.create(r1, "az-" + 2, "az-" + 2, "subnet-" + 2);
    AvailabilityZone az3 = AvailabilityZone.create(r2, "az-" + 3, "az-" + 3, "subnet-" + 3);
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.uuid, pi);
    PlacementInfoUtil.addPlacementZone(az2.uuid, pi);
    PlacementInfoUtil.addPlacementZone(az3.uuid, pi);

    String nodePrefix1 =
        String.format("%s-%s", defaultUniverse.getUniverseDetails().nodePrefix, "az-1");
    String nodePrefix2 =
        String.format("%s-%s", defaultUniverse.getUniverseDetails().nodePrefix, "az-2");
    String nodePrefix3 =
        String.format("%s-%s", defaultUniverse.getUniverseDetails().nodePrefix, "az-3");

    String ns1 = nodePrefix1;
    String ns2 = nodePrefix2;
    String ns3 = nodePrefix3;

    Map<String, String> config1 = new HashMap();
    Map<String, String> config2 = new HashMap();
    Map<String, String> config3 = new HashMap();
    config1.put("KUBECONFIG", "test-kc-" + 1);
    config2.put("KUBECONFIG", "test-kc-" + 2);
    config3.put("KUBECONFIG", "test-kc-" + 3);

    if (setNamespace) {
      ns1 = "demo-ns-1";
      ns2 = "demons2";

      config1.put("KUBENAMESPACE", ns1);
      config2.put("KUBENAMESPACE", ns2);
    }

    az1.updateConfig(config1);
    az2.updateConfig(config2);
    az3.updateConfig(config3);

    String podInfosMessage =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\","
            + " \"podIP\": \"123.456.78.90\"}, \"spec\": {\"hostname\": \"yb-master-0\"},"
            + " \"metadata\": {\"namespace\": \"%1$s\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"123.456.78.91\"}, \"spec\": {\"hostname\": \"yb-tserver-0\"},"
            + " \"metadata\": {\"namespace\": \"%1$s\"}}]}";
    ShellResponse shellResponse1 = ShellResponse.create(0, String.format(podInfosMessage, ns1));
    when(kubernetesManager.getPodInfos(any(), eq(nodePrefix1), eq(ns1))).thenReturn(shellResponse1);
    ShellResponse shellResponse2 = ShellResponse.create(0, String.format(podInfosMessage, ns2));
    when(kubernetesManager.getPodInfos(any(), eq(nodePrefix2), eq(ns2))).thenReturn(shellResponse2);
    ShellResponse shellResponse3 = ShellResponse.create(0, String.format(podInfosMessage, ns3));
    when(kubernetesManager.getPodInfos(any(), eq(nodePrefix3), eq(ns3))).thenReturn(shellResponse3);

    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(KubernetesCommandExecutor.CommandType.POD_INFO, pi);
    kubernetesCommandExecutor.run();

    verify(kubernetesManager, times(1)).getPodInfos(config1, nodePrefix1, ns1);
    verify(kubernetesManager, times(1)).getPodInfos(config2, nodePrefix2, ns2);
    verify(kubernetesManager, times(1)).getPodInfos(config3, nodePrefix3, ns3);
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.universeUUID);

    Map<String, String> podToNamespace = new HashMap();
    podToNamespace.put("yb-master-0_az-1", ns1);
    podToNamespace.put("yb-master-0_az-2", ns2);
    podToNamespace.put("yb-master-0_az-3", ns3);
    podToNamespace.put("yb-tserver-0_az-1", ns1);
    podToNamespace.put("yb-tserver-0_az-2", ns2);
    podToNamespace.put("yb-tserver-0_az-3", ns3);

    Map<String, String> azToRegion = new HashMap();
    azToRegion.put("az-1", "region-1");
    azToRegion.put("az-2", "region-1");
    azToRegion.put("az-3", "region-2");

    for (Map.Entry<String, String> entry : podToNamespace.entrySet()) {
      String podName = entry.getKey();
      String namespace = entry.getValue();
      NodeDetails node = defaultUniverse.getNode(podName);
      assertNotNull(node);
      String serviceName = podName.contains("master") ? "yb-masters" : "yb-tservers";

      assertTrue(podName.contains("master") ? node.isMaster : node.isTserver);

      String az = podName.split("_")[1];
      String podK8sName = podName.split("_")[0];
      assertEquals(
          node.cloudInfo.private_ip,
          String.format("%s.%s.%s.%s", podK8sName, serviceName, namespace, "svc.cluster.local"));
      assertEquals(node.cloudInfo.az, az);
      assertEquals(node.cloudInfo.region, azToRegion.get(az));
    }
  }

  @Test
  public void testHelmInstallLegacy() throws IOException {
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message =
        "{\"items\": [{\"metadata\": {\"name\": \"test\"}, \"spec\": {\"clusterIP\": \"None\","
            + "\"type\":\"clusterIP\"}}]}";
    when(kubernetesManager.getServices(any(), any(), any())).thenReturn(shellResponse);
    defaultUniverse.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V2TO3.toString()));
    assertEquals(hackPlacementUUID, defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid);
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL,
            defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo);
    kubernetesCommandExecutor.taskParams().namespace = namespace;
    kubernetesCommandExecutor.run();
    assertEquals(hackPlacementUUID, defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid);

    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<HashMap> expectedConfig = ArgumentCaptor.forClass(HashMap.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    verify(kubernetesManager, times(1))
        .getServices(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedNamespace.capture());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(hackPlacementUUID, defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid);
    assertEquals(defaultProvider.uuid, expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());
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
  public void testNullNamespaceParameter() {
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_DELETE, /* set namespace */ false);
    try {
      kubernetesCommandExecutor.run();
    } catch (IllegalArgumentException e) {
      assertEquals("namespace can be null only in case of POD_INFO", e.getMessage());
    }
  }
}
