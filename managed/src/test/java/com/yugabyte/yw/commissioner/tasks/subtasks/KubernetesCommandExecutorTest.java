// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ExposingServiceState;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.operator.OperatorConfig;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.common.ShellKubernetesManager;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.alerts.AlertConfigurationWriter;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ExposingServiceState;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent.K8SNodeResourceSpec;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.PodList;
import io.fabric8.kubernetes.api.model.Service;
import io.fabric8.kubernetes.api.model.ServiceList;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import kamon.instrumentation.play.GuiceModule;
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

public class KubernetesCommandExecutorTest extends SubTaskBaseTest {
  private static final String CERTS_DIR = "/tmp/yugaware_tests/kcet_certs";

  ShellKubernetesManager kubernetesManager;
  Provider defaultProvider;
  Universe defaultUniverse;
  Region defaultRegion;
  AvailabilityZone defaultAZ;
  CertificateInfo defaultCert;
  CertificateHelper certificateHelper;
  RuntimeConfGetter runtimeConfGetter;
  UniverseDefinitionTaskParams.UserIntent defaultUserIntent;
  InstanceType instanceType;
  String ybSoftwareVersion = "1.0.0";
  int numNodes = 3;
  String namespace = "demo-ns";
  Map<String, String> config = new HashMap<>();

  protected CallbackController mockCallbackController;
  protected PlayCacheSessionStore mockSessionStore;
  protected AlertConfigurationWriter mockAlertConfigurationWriter;

  @Override
  protected Application provideApplication() {
    kubernetesManager = mock(ShellKubernetesManager.class);
    mockCallbackController = mock(CallbackController.class);
    mockSessionStore = mock(PlayCacheSessionStore.class);
    mockAlertConfigurationWriter = mock(AlertConfigurationWriter.class);
    return new GuiceApplicationBuilder()
        .disable(GuiceModule.class)
        .configure(testDatabase())
        .overrides(bind(ShellKubernetesManager.class).toInstance(kubernetesManager))
        .overrides(bind(CallbackController.class).toInstance(mockCallbackController))
        .overrides(bind(PlaySessionStore.class).toInstance(mockSessionStore))
        .overrides(bind(AlertConfigurationWriter.class).toInstance(mockAlertConfigurationWriter))
        .build();
  }

  @Before
  public void setUp() {
    super.setUp();
    defaultProvider = ModelFactory.kubernetesProvider(defaultCustomer);
    defaultRegion =
        Region.create(defaultProvider, "region-1", "PlacementRegion 1", "default-image");
    defaultAZ = AvailabilityZone.createOrThrow(defaultRegion, "az-1", "PlacementAZ 1", "subnet-1");
    config.put("KUBECONFIG", "test");
    defaultAZ.updateConfig(config);
    defaultAZ.save();
    defaultUniverse = ModelFactory.createUniverse("demo-universe", defaultCustomer.getId());
    defaultUniverse = updateUniverseDetails("small");
    new File(CERTS_DIR).mkdirs();
    Config spyConf = spy(app.config());
    doReturn(CERTS_DIR).when(spyConf).getString("yb.storage.path");
    runtimeConfGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    certificateHelper = new CertificateHelper(app.injector().instanceOf(RuntimeConfGetter.class));
    defaultCert =
        CertificateInfo.get(
            certificateHelper.createRootCA(
                spyConf,
                defaultUniverse.getUniverseDetails().nodePrefix,
                defaultProvider.getCustomerUUID()));
    defaultUniverse.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    defaultUniverse.save();
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(CERTS_DIR));
  }

  private Universe updateUniverseDetails(String instanceTypeCode) {
    instanceType =
        InstanceType.upsert(
            defaultProvider.getUuid(),
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
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(defaultUserIntent, "host", true));
    return u;
  }

  private KubernetesCommandExecutor createExecutor(
      KubernetesCommandExecutor.CommandType commandType, boolean setNamespace) {
    KubernetesCommandExecutor kubernetesCommandExecutor =
        AbstractTaskBase.createTask(KubernetesCommandExecutor.class);
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    params.ybSoftwareVersion = ybSoftwareVersion;
    params.providerUUID = defaultProvider.getUuid();
    params.commandType = commandType;
    params.config = config;
    params.universeName = defaultUniverse.getName();
    params.helmReleaseName = defaultUniverse.getUniverseDetails().nodePrefix;
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.universeConfig = defaultUniverse.getConfig();
    params.universeDetails = defaultUniverse.getUniverseDetails();
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
    params.ybSoftwareVersion = ybSoftwareVersion;
    params.providerUUID = defaultProvider.getUuid();
    params.commandType = commandType;
    params.helmReleaseName = defaultUniverse.getUniverseDetails().nodePrefix;
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.config = config;
    params.universeName = defaultUniverse.getName();
    params.universeConfig = defaultUniverse.getConfig();
    params.universeDetails = defaultUniverse.getUniverseDetails();
    params.placementInfo = placementInfo;
    kubernetesCommandExecutor.initialize(params);
    return kubernetesCommandExecutor;
  }

  private Map<String, Object> getExpectedOverrides(boolean exposeAll) {
    Yaml yaml = new Yaml();
    Map<String, Object> expectedOverrides = new HashMap<>();
    if (exposeAll) {
      expectedOverrides = yaml.load(app.environment().resourceAsStream("k8s-expose-all.yml"));
    }
    double burstVal = 1.2;
    Map<String, String> config = CloudInfoInterface.fetchEnvVars(defaultProvider);

    Map<String, Object> storageOverrides =
        (Map<String, Object>) expectedOverrides.getOrDefault("storage", new HashMap<>());
    if (defaultUserIntent.deviceInfo != null) {
      Map<String, Object> tserverDiskSpecs =
          (Map<String, Object>) storageOverrides.getOrDefault("tserver", new HashMap<>());
      Map<String, Object> masterDiskSpecs =
          (Map<String, Object>) storageOverrides.getOrDefault("master", new HashMap<>());

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

    Map<String, Object> resourceOverrides = new HashMap<>();

    Map<String, Object> tserverResource = new HashMap<>();
    Map<String, Object> tserverLimit = new HashMap<>();
    Map<String, Object> masterResource = new HashMap<>();
    Map<String, Object> masterLimit = new HashMap<>();

    if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.usek8sCustomResources)) {
      tserverResource.put("cpu", instanceType.getNumCores());
      tserverResource.put("memory", String.format("%.2fGi", instanceType.getMemSizeGB()));
      tserverLimit.put("cpu", instanceType.getNumCores() * burstVal);
      tserverLimit.put("memory", String.format("%.2fGi", instanceType.getMemSizeGB()));
      resourceOverrides.put(
          "tserver", ImmutableMap.of("requests", tserverResource, "limits", tserverLimit));

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
    } else {
      // Instantiate the default and get values from it.
      K8SNodeResourceSpec k8sResourceSpec = new K8SNodeResourceSpec();
      masterResource.put("memory", String.format("%.2f%s", k8sResourceSpec.memoryGib, "Gi"));
      masterResource.put("cpu", String.format("%.2f", k8sResourceSpec.cpuCoreCount));
      tserverResource.put("memory", String.format("%.2f%s", k8sResourceSpec.memoryGib, "Gi"));
      tserverResource.put("cpu", String.format("%.2f", k8sResourceSpec.cpuCoreCount));
      masterLimit.put("memory", String.format("%.2f%s", k8sResourceSpec.memoryGib, "Gi"));
      masterLimit.put("cpu", String.format("%.2f", k8sResourceSpec.cpuCoreCount));
      tserverLimit.put("memory", String.format("%.2f%s", k8sResourceSpec.memoryGib, "Gi"));
      tserverLimit.put("cpu", String.format("%.2f", k8sResourceSpec.cpuCoreCount));
      resourceOverrides.put(
          "master", ImmutableMap.of("requests", masterResource, "limits", masterLimit));
      resourceOverrides.put(
          "tserver", ImmutableMap.of("requests", tserverResource, "limits", tserverLimit));
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
    Map<String, Object> masterGFlags = new HashMap<>(defaultUserIntent.masterGFlags);
    masterGFlags.put("placement_cloud", defaultProvider.getCode());
    masterGFlags.put("placement_region", defaultRegion.getCode());
    masterGFlags.put("placement_zone", defaultAZ.getCode());
    masterGFlags.put(
        "placement_uuid", defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid.toString());
    gflagOverrides.put("master", masterGFlags);

    // Tserver flags.
    Map<String, Object> tserverGFlags = new HashMap<>(defaultUserIntent.tserverGFlags);
    tserverGFlags.put("placement_cloud", defaultProvider.getCode());
    tserverGFlags.put("placement_region", defaultRegion.getCode());
    tserverGFlags.put("placement_zone", defaultAZ.getCode());
    tserverGFlags.put(
        "placement_uuid", defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid.toString());
    tserverGFlags.put("start_redis_proxy", "true");
    gflagOverrides.put("tserver", tserverGFlags);
    // Put all the flags together.
    expectedOverrides.put("gflags", gflagOverrides);

    Map<String, Object> ybcOverrides = new HashMap<>();
    ybcOverrides.put("enabled", false);
    expectedOverrides.put("ybc", ybcOverrides);

    Map<String, String> universeConfig = defaultUniverse.getConfig();
    if (universeConfig.getOrDefault(Universe.LABEL_K8S_RESOURCES, "false").equals("true")) {
      expectedOverrides.put(
          "commonLabels",
          ImmutableMap.of(
              "yugabyte.io/universe-name", "demo-universe",
              "app.kubernetes.io/part-of", "demo-universe",
              "yugabyte.io/zone", "az-1"));
    }

    Map<String, String> regionConfig = CloudInfoInterface.fetchEnvVars(defaultRegion);
    Map<String, String> azConfig = CloudInfoInterface.fetchEnvVars(defaultAZ);

    String providerOverridesYAML = null;
    if (!azConfig.containsKey("OVERRIDES")) {
      if (!regionConfig.containsKey("OVERRIDES")) {
        if (config.containsKey("OVERRIDES")) {
          providerOverridesYAML = config.get("OVERRIDES");
        }
      } else {
        providerOverridesYAML = regionConfig.get("OVERRIDES");
      }
    } else {
      providerOverridesYAML = azConfig.get("OVERRIDES");
    }

    if (providerOverridesYAML != null) {
      Map<String, Object> providerOverrides = yaml.load(providerOverridesYAML);
      if (providerOverrides != null) {
        expectedOverrides.putAll(providerOverrides);
      }
    }
    expectedOverrides.put("disableYsql", !defaultUserIntent.enableYSQL);
    boolean helmLegacy =
        Universe.HelmLegacy.valueOf(universeConfig.get(Universe.HELM2_LEGACY))
            == Universe.HelmLegacy.V2TO3;
    if (helmLegacy) {
      expectedOverrides.put("helm2Legacy", helmLegacy);
    }
    if (defaultUniverse.getUniverseDetails().useNewHelmNamingStyle) {
      expectedOverrides.put("oldNamingStyle", false);
      expectedOverrides.put("fullnameOverride", "host");
    }
    Map<String, Object> yugabytedUiInfo = new HashMap<>();
    Map<String, Object> metricsSnapshotterInfo = new HashMap<>();
    boolean COMMUNITY_OP_ENABLED = OperatorConfig.getOssMode();
    metricsSnapshotterInfo.put("enabled", COMMUNITY_OP_ENABLED);
    yugabytedUiInfo.put("enabled", COMMUNITY_OP_ENABLED);
    yugabytedUiInfo.put("metricsSnapshotter", metricsSnapshotterInfo);
    expectedOverrides.put("yugabytedUi", yugabytedUiInfo);

    expectedOverrides.put("defaultServiceScope", "AZ");
    return expectedOverrides;
  }

  @Test
  public void testHelmInstall() throws IOException {
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(ybSoftwareVersion, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultProvider.getUuid(), expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());
    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    // TODO implement exposeAll false case
    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallIPV6() throws IOException {
    defaultUserIntent.enableIPV6 = true;
    Universe u =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(defaultUserIntent, "host", true));

    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(ybSoftwareVersion, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultProvider.getUuid(), expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
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
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(defaultUserIntent, "host", true));

    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(ybSoftwareVersion, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultProvider.getUuid(), expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());
    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallWithGflags() throws IOException {
    defaultUserIntent.masterGFlags = new HashMap<>(ImmutableMap.of("yb-master-flag", "demo-flag"));
    defaultUserIntent.tserverGFlags =
        new HashMap<>(ImmutableMap.of("yb-tserver-flag", "demo-flag"));
    defaultUserIntent.ybSoftwareVersion = ybSoftwareVersion;
    Universe u =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(defaultUserIntent, "host", true));

    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(ybSoftwareVersion, expectedYbSoftwareVersion.getValue());
    assertEquals(defaultProvider.getUuid(), expectedProviderUUID.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);
    // TODO implement exposeAll false case
    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallWithTLS() throws IOException {
    defaultUserIntent.masterGFlags = new HashMap<>(ImmutableMap.of("yb-master-flag", "demo-flag"));
    defaultUserIntent.tserverGFlags =
        new HashMap<>(ImmutableMap.of("yb-tserver-flag", "demo-flag"));
    defaultUserIntent.ybSoftwareVersion = ybSoftwareVersion;
    defaultUserIntent.enableNodeToNodeEncrypt = true;
    defaultUserIntent.enableClientToNodeEncrypt = true;
    defaultUniverse.getUniverseDetails().upsertPrimaryCluster(defaultUserIntent, null);
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(), ApiUtils.mockUniverseUpdater(defaultCert.getUuid()));
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(ybSoftwareVersion, expectedYbSoftwareVersion.getValue());
    assertEquals(defaultProvider.getUuid(), expectedProviderUUID.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    // TODO implement exposeAll false case
    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallWithTLSNodeToNode() throws IOException {
    defaultUserIntent.masterGFlags = new HashMap<>(ImmutableMap.of("yb-master-flag", "demo-flag"));
    defaultUserIntent.tserverGFlags =
        new HashMap<>(ImmutableMap.of("yb-tserver-flag", "demo-flag"));
    defaultUserIntent.ybSoftwareVersion = ybSoftwareVersion;
    defaultUserIntent.enableNodeToNodeEncrypt = true;
    defaultUserIntent.enableClientToNodeEncrypt = false;
    defaultUniverse.getUniverseDetails().upsertPrimaryCluster(defaultUserIntent, null);
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(), ApiUtils.mockUniverseUpdater(defaultCert.getUuid()));
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(ybSoftwareVersion, expectedYbSoftwareVersion.getValue());
    assertEquals(defaultProvider.getUuid(), expectedProviderUUID.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    // TODO implement exposeAll false case
    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallWithTLSClientToServer() throws IOException {
    defaultUserIntent.masterGFlags = new HashMap<>(ImmutableMap.of("yb-master-flag", "demo-flag"));
    defaultUserIntent.tserverGFlags =
        new HashMap<>(ImmutableMap.of("yb-tserver-flag", "demo-flag"));
    defaultUserIntent.ybSoftwareVersion = ybSoftwareVersion;
    defaultUserIntent.enableNodeToNodeEncrypt = false;
    defaultUserIntent.enableClientToNodeEncrypt = true;
    defaultUniverse.getUniverseDetails().upsertPrimaryCluster(defaultUserIntent, null);
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(), ApiUtils.mockUniverseUpdater(defaultCert.getUuid()));
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(ybSoftwareVersion, expectedYbSoftwareVersion.getValue());
    assertEquals(defaultProvider.getUuid(), expectedProviderUUID.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
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

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(ybSoftwareVersion, expectedYbSoftwareVersion.getValue());
    assertEquals(defaultProvider.getUuid(), expectedProviderUUID.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);
    Map<String, Object> resourceOverrides = (Map<String, Object>) overrides.get("resource");
    if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.usek8sCustomResources)) {
      if (instanceType.equals("dev")) {
        assertTrue(resourceOverrides.containsKey("master"));
      } else {
        assertFalse(resourceOverrides.containsKey("master"));
      }
    } else {
      // We are adding master to resource overrides default values
      assertTrue(resourceOverrides.containsKey("master"));
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
    Map<String, String> defaultAnnotations = new HashMap<>();
    defaultAnnotations.put(
        "OVERRIDES",
        "serviceEndpoints:\n"
            + "  - name: yb-master-service\n"
            + "    type: LoadBalancer\n"
            + "    app: yb-master\n"
            + "    annotations:\n"
            + "      annotation-1: foo\n"
            + "    ports:\n"
            + "      ui: 7000\n\n"
            + "  - name: yb-tserver-service\n"
            + "    type: LoadBalancer\n"
            + "    app: yb-tserver\n"
            + "    ports:\n"
            + "      ycql-port: 9042\n"
            + "      yedis-port: 6379");
    defaultProvider.setConfigMap(defaultAnnotations);
    defaultProvider.save();
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(ybSoftwareVersion, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultProvider.getUuid(), expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    // TODO implement exposeAll false case
    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallForAnnotationsRegion() throws IOException {
    Map<String, String> defaultAnnotations = new HashMap<>();
    defaultAnnotations.put(
        "OVERRIDES",
        "serviceEndpoints:\n"
            + "  - name: yb-master-service\n"
            + "    type: LoadBalancer\n"
            + "    app: yb-master\n"
            + "    annotations:\n"
            + "      annotation-1: bar\n"
            + "    ports:\n"
            + "      ui: 7000\n\n"
            + "  - name: yb-tserver-service\n"
            + "    type: LoadBalancer\n"
            + "    app: yb-tserver\n"
            + "    ports:\n"
            + "      ycql-port: 9042\n"
            + "      yedis-port: 6379");
    defaultRegion.setConfig(defaultAnnotations);
    defaultRegion.save();
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(ybSoftwareVersion, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultProvider.getUuid(), expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    // TODO implement exposeAll false case
    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallForAnnotationsAZ() throws IOException {
    Map<String, String> defaultAnnotations = new HashMap<>();
    defaultAnnotations.put(
        "OVERRIDES",
        "serviceEndpoints:\n"
            + "  - name: yb-master-service\n"
            + "    type: LoadBalancer\n"
            + "    app: yb-master\n"
            + "    annotations:\n"
            + "      annotation-1: bar\n"
            + "    ports:\n"
            + "      ui: 7000\n\n"
            + "  - name: yb-tserver-service\n"
            + "    type: LoadBalancer\n"
            + "    app: yb-tserver\n"
            + "    ports:\n"
            + "      ycql-port: 9042\n"
            + "      yedis-port: 6379");
    defaultAZ.updateConfig(defaultAnnotations);
    defaultAZ.save();
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(ybSoftwareVersion, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultProvider.getUuid(), expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    // TODO implement exposeAll false case
    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallForAnnotationsPrecendence() throws IOException {
    Map<String, String> defaultAnnotations = new HashMap<>();
    defaultAnnotations.put("OVERRIDES", "foo: bar");
    defaultProvider.setConfigMap(defaultAnnotations);
    defaultProvider.save();
    defaultAnnotations.put("OVERRIDES", "bar: foo");
    defaultAZ.updateConfig(defaultAnnotations);
    defaultAZ.save();

    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(ybSoftwareVersion, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultProvider.getUuid(), expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
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
    Map<String, String> defaultAnnotations = new HashMap<>();
    defaultAnnotations.put("OVERRIDES", "resource:\n  master:\n    limits:\n      cpu: 650m");
    defaultAZ.updateConfig(defaultAnnotations);
    defaultAZ.save();
    defaultUniverse = updateUniverseDetails("dev");
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(ybSoftwareVersion, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultProvider.getUuid(), expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
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

    if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.usek8sCustomResources)) {
      tserverResource.put("cpu", instanceType.getNumCores());
      tserverResource.put("memory", String.format("%.2fGi", instanceType.getMemSizeGB()));
      tserverLimit.put("cpu", instanceType.getNumCores() * burstVal);
      tserverLimit.put("memory", String.format("%.2fGi", instanceType.getMemSizeGB()));

      masterResource.put("cpu", 0.5);
      masterResource.put("memory", "0.5Gi");
      masterLimit.put("cpu", "650m");
      masterLimit.put("memory", "0.5Gi");
    } else {
      // Get Defaults form k8sResourceSpec
      K8SNodeResourceSpec k8sResourceSpec = new K8SNodeResourceSpec();
      masterResource.put("memory", String.format("%.2f%s", k8sResourceSpec.memoryGib, "Gi"));
      masterResource.put("cpu", String.format("%.2f", k8sResourceSpec.cpuCoreCount));
      tserverResource.put("memory", String.format("%.2f%s", k8sResourceSpec.memoryGib, "Gi"));
      tserverResource.put("cpu", String.format("%.2f", k8sResourceSpec.cpuCoreCount));
      masterLimit.put("memory", String.format("%.2f%s", k8sResourceSpec.memoryGib, "Gi"));
      // Verify that master limit for CPU is what we set above in overrides
      masterLimit.put("cpu", "650m");
      tserverLimit.put("memory", String.format("%.2f%s", k8sResourceSpec.memoryGib, "Gi"));
      tserverLimit.put("cpu", String.format("%.2f", k8sResourceSpec.cpuCoreCount));
    }
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
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(defaultUserIntent, "host", true));
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(ybSoftwareVersion, expectedYbSoftwareVersion.getValue());
    assertEquals(defaultProvider.getUuid(), expectedProviderUUID.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    // TODO implement exposeAll false case
    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallNewNaming() throws IOException {
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithHelmNamingStyle(true));

    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(ybSoftwareVersion, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultProvider.getUuid(), expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    assertEquals(getExpectedOverrides(true), overrides);
  }

  @Test
  public void testHelmInstallResourceLabels() throws IOException {
    defaultUniverse.updateConfig(ImmutableMap.of(Universe.LABEL_K8S_RESOURCES, "true"));
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL, /* set namespace */ true);
    kubernetesCommandExecutor.run();

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    assertEquals(ybSoftwareVersion, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultProvider.getUuid(), expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());
    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
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
    testPodInfoBase(false, false);
  }

  @Test
  public void testPodInfoWithNamespace() {
    testPodInfoBase(true, false);
  }

  @Test
  public void testPodInfoWithNewNaming() {
    testPodInfoBase(false, true);
  }

  private void testPodInfoBase(boolean setNamespace, boolean newNamingStyle) {
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
    defaultAZ.save();

    String helmNameSuffix = "";
    if (newNamingStyle) {
      helmNameSuffix = nodePrefix + "-yugabyte-";
    }

    String podsString =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\","
            + " \"podIP\": \"123.456.78.90\"}, \"spec\": {\"hostname\": \"%1$syb-master-0\"},"
            + " \"metadata\": {\"namespace\": \"%2$s\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"123.456.78.91\"}, \"spec\": {\"hostname\": \"%1$syb-tserver-0\"},"
            + " \"metadata\": {\"namespace\": \"%2$s\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"123.456.78.92\"}, \"spec\": {\"hostname\": \"%1$syb-master-1\"},"
            + " \"metadata\": {\"namespace\": \"%2$s\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\","
            + " \"podIP\": \"123.456.78.93\"}, \"spec\": {\"hostname\": \"%1$syb-tserver-1\"},"
            + " \"metadata\": {\"namespace\": \"%2$s\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"123.456.78.94\"}, \"spec\": {\"hostname\": \"%1$syb-master-2\"},"
            + " \"metadata\": {\"namespace\": \"%2$s\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"123.456.78.95\"}, \"spec\": {\"hostname\": \"%1$syb-tserver-2\"},"
            + " \"metadata\": {\"namespace\": \"%2$s\"}}]}";
    List<Pod> podList =
        TestUtils.deserialize(String.format(podsString, helmNameSuffix, namespace), PodList.class)
            .getItems();
    when(kubernetesManager.getPodInfos(any(), any(), any())).thenReturn(podList);
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.POD_INFO,
            defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo);
    assertEquals(3, defaultUniverse.getNodes().size());
    kubernetesCommandExecutor.run();
    verify(kubernetesManager, times(1)).getPodInfos(azConfig, nodePrefix, namespace);
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    ImmutableList<String> nodeNames =
        ImmutableList.of(
            "yb-master-0",
            "yb-master-1",
            "yb-master-2",
            "yb-tserver-0",
            "yb-tserver-1",
            "yb-tserver-2");
    for (String nodeName : nodeNames) {
      NodeDetails node = defaultUniverse.getNode(nodeName);
      assertNotNull(node);
      String podName = helmNameSuffix + nodeName;
      String serviceName = podName.contains("master") ? "yb-masters" : "yb-tservers";
      assertTrue(podName.contains("master") ? node.isMaster : node.isTserver);
      assertEquals(
          node.cloudInfo.private_ip,
          String.format(
              "%s.%s%s.%s.%s",
              podName, helmNameSuffix, serviceName, namespace, "svc.cluster.local"));
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
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r1, "az-" + 1, "az-" + 1, "subnet-" + 1);
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(r1, "az-" + 2, "az-" + 2, "subnet-" + 2);
    AvailabilityZone az3 = AvailabilityZone.createOrThrow(r2, "az-" + 3, "az-" + 3, "subnet-" + 3);
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi);

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
    az1.save();
    az2.updateConfig(config2);
    az2.save();
    az3.updateConfig(config3);
    az3.save();

    String podInfosMessage =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\","
            + " \"podIP\": \"123.456.78.90\"}, \"spec\": {\"hostname\": \"yb-master-0\"},"
            + " \"metadata\": {\"namespace\": \"%1$s\"}},"
            + "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", "
            + "\"podIP\": \"123.456.78.91\"}, \"spec\": {\"hostname\": \"yb-tserver-0\"},"
            + " \"metadata\": {\"namespace\": \"%1$s\"}}]}";
    List<Pod> pods1 =
        TestUtils.deserialize(String.format(podInfosMessage, ns1), PodList.class).getItems();
    when(kubernetesManager.getPodInfos(any(), eq(nodePrefix1), eq(ns1))).thenReturn(pods1);
    List<Pod> pods2 =
        TestUtils.deserialize(String.format(podInfosMessage, ns2), PodList.class).getItems();
    when(kubernetesManager.getPodInfos(any(), eq(nodePrefix2), eq(ns2))).thenReturn(pods2);
    List<Pod> pods3 =
        TestUtils.deserialize(String.format(podInfosMessage, ns3), PodList.class).getItems();
    when(kubernetesManager.getPodInfos(any(), eq(nodePrefix3), eq(ns3))).thenReturn(pods3);

    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(KubernetesCommandExecutor.CommandType.POD_INFO, pi);
    kubernetesCommandExecutor.run();

    verify(kubernetesManager, times(1)).getPodInfos(config1, nodePrefix1, ns1);
    verify(kubernetesManager, times(1)).getPodInfos(config2, nodePrefix2, ns2);
    verify(kubernetesManager, times(1)).getPodInfos(config3, nodePrefix3, ns3);
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());

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
    String servicesString =
        "{\"items\": [{\"metadata\": {\"name\": \"test\"}, \"spec\": {\"clusterIP\": \"None\","
            + "\"type\":\"clusterIP\"}}]}";
    List<Service> services = TestUtils.deserialize(servicesString, ServiceList.class).getItems();
    when(kubernetesManager.getServices(any(), any(), any())).thenReturn(services);
    defaultUniverse.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V2TO3.toString()));
    KubernetesCommandExecutor kubernetesCommandExecutor =
        createExecutor(
            KubernetesCommandExecutor.CommandType.HELM_INSTALL,
            defaultUniverse.getUniverseDetails().getPrimaryCluster().placementInfo);
    kubernetesCommandExecutor.taskParams().namespace = namespace;
    kubernetesCommandExecutor.run();

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<UUID> expectedProviderUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    verify(kubernetesManager, times(1))
        .helmInstall(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedProviderUUID.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    verify(kubernetesManager, times(1))
        .getServices(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedNamespace.capture());
    assertEquals(ybSoftwareVersion, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(defaultProvider.getUuid(), expectedProviderUUID.getValue());
    assertEquals(defaultUniverse.getUniverseDetails().nodePrefix, expectedNodePrefix.getValue());
    assertEquals(namespace, expectedNamespace.getValue());
    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);

    // TODO implement exposeAll false case
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
