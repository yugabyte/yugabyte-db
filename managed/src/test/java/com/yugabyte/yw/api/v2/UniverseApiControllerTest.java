// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.ModelFactory.createFromConfig;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.hamcrest.Matchers.hasSize;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.api.UniverseApi;
import com.yugabyte.yba.v2.client.models.AvailabilityZoneNodeSpec;
import com.yugabyte.yba.v2.client.models.CheckResizeOptionsResp;
import com.yugabyte.yba.v2.client.models.CheckResizeOptionsSpec;
import com.yugabyte.yba.v2.client.models.ClusterNodeSpec;
import com.yugabyte.yba.v2.client.models.ClusterPerProcessNodeSpec;
import com.yugabyte.yba.v2.client.models.ClusterSpec;
import com.yugabyte.yba.v2.client.models.ClusterStorageBase;
import com.yugabyte.yba.v2.client.models.ClusterStorageSpec;
import com.yugabyte.yba.v2.client.models.PerProcessNodeSpec;
import com.yugabyte.yba.v2.client.models.PerProviderResizeNodesSpec;
import com.yugabyte.yba.v2.client.models.ResizeUpdateOption;
import com.yugabyte.yba.v2.client.models.UniverseCreateSpec;
import com.yugabyte.yba.v2.client.models.UniverseDeleteSpec;
import com.yugabyte.yba.v2.client.models.UniverseSettings;
import com.yugabyte.yba.v2.client.models.YBATask;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.DestroyUniverse;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.AZOverrides;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PerProcessDetails;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntentOverrides;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicReference;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;

/** Tests for Create/Get/Delete of Universe using v2.UniverseApiControllerImp */
@Slf4j
@RunWith(JUnitParamsRunner.class)
public class UniverseApiControllerTest extends UniverseTestBase {

  private void stubGFlagsValidation() throws IOException {
    when(mockGFlagsValidation.getGFlagDetails(anyString(), anyString(), anyString()))
        .thenReturn(Optional.empty());
  }

  @Test
  public void testGetUniverseV2() throws ApiException, IOException {
    UUID uUUID = createUniverse(customer.getId()).getUniverseUUID();
    Universe dbUniverse =
        Universe.saveDetails(
            uUUID,
            universe -> {
              // arch
              universe.getUniverseDetails().arch = PublicCloudConstants.Architecture.aarch64;
              // systemd
              universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd = true;
              // ysql
              universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL = true;
              universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQLAuth = true;
              universe.getUniverseDetails().getPrimaryCluster().userIntent.dedicatedNodes = true;
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ysqlPassword =
                  "password#1";
              // regionList
              List<Region> regions = Region.getByProvider(providerUuid);
              universe.getUniverseDetails().getPrimaryCluster().userIntent.regionList =
                  regions != null
                      ? regions.stream().map(r -> r.getUuid()).toList()
                      : new ArrayList<>();
              // Volumes
              universe.getUniverseDetails().getPrimaryCluster().userIntent.deviceInfo =
                  ApiUtils.getDummyDeviceInfo(2, 150);
              universe.getUniverseDetails().getPrimaryCluster().userIntent.masterInstanceType =
                  "c5.4xlarge";
              universe.getUniverseDetails().getPrimaryCluster().userIntent.masterDeviceInfo =
                  ApiUtils.getDummyDeviceInfo(1, 50);
              PerProcessDetails tserverDetails = new PerProcessDetails();
              tserverDetails.setInstanceType("c5.2xlarge");
              tserverDetails.setDeviceInfo(ApiUtils.getDummyDeviceInfo(2, 200));
              UserIntentOverrides userIntentOverrides = new UserIntentOverrides();
              userIntentOverrides.setPerProcess(Map.of(ServerType.TSERVER, tserverDetails));
              universe
                  .getUniverseDetails()
                  .getPrimaryCluster()
                  .userIntent
                  .setUserIntentOverrides(userIntentOverrides);
              // instanceTags
              universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags =
                  Map.of("tag1", "value1", "tag2", "value2");
              // instanceType
              universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType =
                  ApiUtils.UTIL_INST_TYPE;
              // GFlags
              universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags =
                  SpecificGFlags.construct(
                      Map.of("mflag1", "mval1", "mflag2", "mval2"),
                      Map.of("tflag1", "tval1", "tflag2", "tval2"));
              SpecificGFlags.PerProcessFlags azFlags = new SpecificGFlags.PerProcessFlags();
              azFlags.value.put(ServerType.MASTER, Map.of("mperaz1", "val1", "mperaz2", "val2"));
              azFlags.value.put(ServerType.TSERVER, Map.of("tperaz1", "v1", "tperaz2", "v2"));
              universe
                  .getUniverseDetails()
                  .getPrimaryCluster()
                  .userIntent
                  .specificGFlags
                  .setPerAZ(
                      Map.of(universe.getUniverseDetails().getPrimaryCluster().uuid, azFlags));
            });
    stubGFlagsValidation();
    UniverseApi api = new UniverseApi();
    com.yugabyte.yba.v2.client.models.Universe universeResp =
        api.getUniverse(customer.getUuid(), uUUID);
    validateUniverseSpec(universeResp.getSpec(), dbUniverse);
    validateUniverseInfo(universeResp.getInfo(), dbUniverse);
  }

  @Test
  public void testCreateUniverseV2() throws ApiException, IOException {
    UniverseCreateSpec universeCreateSpec = getUniverseCreateSpecV2();
    AtomicReference<UniverseDefinitionTaskParams> paramsRef = new AtomicReference<>();
    setupUniverse(universeCreateSpec, paramsRef::set);
    UniverseDefinitionTaskParams v1CreateParams = paramsRef.get();

    // validate that the Universe create params matches properties specified in the createSpec
    validateUniverseCreateSpec(universeCreateSpec, v1CreateParams);
  }

  @Test
  public void testCreateUniverseV2WithAzPerProcessOverrides() throws ApiException, IOException {
    UniverseApi api = new UniverseApi();
    UniverseCreateSpec universeCreateSpec = getUniverseCreateSpecV2();
    ClusterNodeSpec nodeSpec = universeCreateSpec.getSpec().getClusters().get(0).getNodeSpec();
    nodeSpec.setDedicatedNodes(true);

    ClusterPerProcessNodeSpec clusterMasterSpec = new ClusterPerProcessNodeSpec();
    clusterMasterSpec.setInstanceType("c5.4xlarge");
    // Explicit master storage so dedicated create does not auto-fill masterDeviceInfo from
    // deviceInfo (which would break validateUniverseCreateSpec).
    clusterMasterSpec.setStorageSpec(
        new ClusterStorageSpec()
            .volumeSize(50)
            .numVolumes(1)
            .storageType(nodeSpec.getStorageSpec().getStorageType()));
    nodeSpec.setMaster(clusterMasterSpec);

    ClusterPerProcessNodeSpec clusterTserverSpec = new ClusterPerProcessNodeSpec();
    clusterTserverSpec.setInstanceType("c5.2xlarge");
    nodeSpec.setTserver(clusterTserverSpec);

    UUID azUUID =
        AvailabilityZone.getAZsForRegion(Region.getByProvider(providerUuid).get(0).getUuid())
            .get(0)
            .getUuid();

    AvailabilityZoneNodeSpec azSpec = new AvailabilityZoneNodeSpec();
    PerProcessNodeSpec azMasterSpec = new PerProcessNodeSpec();
    azMasterSpec.setStorageSpec(new ClusterStorageBase().volumeSize(50).numVolumes(1));
    azSpec.setMaster(azMasterSpec);

    PerProcessNodeSpec azTserverSpec = new PerProcessNodeSpec();
    azTserverSpec.setStorageSpec(
        new ClusterStorageBase().volumeSize(300).diskIops(4000).numVolumes(2));
    azSpec.setTserver(azTserverSpec);

    Map<String, AvailabilityZoneNodeSpec> azNodeSpec = new HashMap<>();
    azNodeSpec.put(azUUID.toString(), azSpec);
    nodeSpec.setAzNodeSpec(azNodeSpec);

    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    when(mockRuntimeConfig.getInt("yb.universe.otel_collector_metrics_port")).thenReturn(8889);
    stubGFlagsValidation();
    YBATask createTask = api.createUniverse(customer.getUuid(), universeCreateSpec);
    assertThat(createTask.getTaskUuid(), is(fakeTaskUUID));
    ArgumentCaptor<UniverseDefinitionTaskParams> v1CreateParamsCapture =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.CreateUniverse), v1CreateParamsCapture.capture());
    UniverseDefinitionTaskParams v1CreateParams = v1CreateParamsCapture.getValue();

    UserIntent userIntent = v1CreateParams.getPrimaryCluster().userIntent;
    assertThat(userIntent.dedicatedNodes, is(true));
    assertThat(userIntent.getUserIntentOverrides(), is(notNullValue()));
    assertThat(userIntent.getUserIntentOverrides().getAzOverrides(), is(notNullValue()));

    AZOverrides azOverrides = userIntent.getUserIntentOverrides().getAzOverrides().get(azUUID);
    assertThat(azOverrides, is(notNullValue()));
    assertThat(azOverrides.getPerProcess(), is(notNullValue()));
    assertThat(azOverrides.getPerProcess().get(ServerType.MASTER), is(notNullValue()));
    assertThat(azOverrides.getPerProcess().get(ServerType.TSERVER), is(notNullValue()));
    assertEquals(
        Integer.valueOf(50),
        azOverrides.getPerProcess().get(ServerType.MASTER).getDeviceInfo().volumeSize);
    assertEquals(
        Integer.valueOf(300),
        azOverrides.getPerProcess().get(ServerType.TSERVER).getDeviceInfo().volumeSize);
    assertEquals(
        Integer.valueOf(4000),
        azOverrides.getPerProcess().get(ServerType.TSERVER).getDeviceInfo().diskIops);

    NodeDetails azMasterNode = new NodeDetails();
    azMasterNode.azUuid = azUUID;
    azMasterNode.dedicatedTo = ServerType.MASTER;
    assertEquals("c5.4xlarge", userIntent.getInstanceTypeForNode(azMasterNode));

    NodeDetails azTserverNode = new NodeDetails();
    azTserverNode.azUuid = azUUID;
    azTserverNode.dedicatedTo = ServerType.TSERVER;
    assertEquals("c5.2xlarge", userIntent.getInstanceTypeForNode(azTserverNode));
    assertEquals(Integer.valueOf(300), userIntent.getDeviceInfoForNode(azTserverNode).volumeSize);

    validateUniverseCreateSpec(universeCreateSpec, v1CreateParams);
  }

  @Test
  public void testCreateUniverseV2WithUniverseSettings() throws ApiException, IOException {
    UniverseCreateSpec universeCreateSpec = getUniverseCreateSpecV2();
    universeCreateSpec.getSpec().universeSettings(new UniverseSettings().expertMode(true));

    UUID uuid = setupUniverse(universeCreateSpec, null);
    Universe dbUniverse = Universe.getOrBadRequest(uuid);
    assertThat(getExpertMode(dbUniverse), is(true));
    UniverseApi api = new UniverseApi();
    assertThat(
        api.getUniverse(customer.getUuid(), uuid).getSpec().getUniverseSettings().getExpertMode(),
        is(true));
  }

  @Test
  public void testCreateUniverseV2Multiprovider() throws ApiException, IOException {
    setNewUIEnabled();
    UniverseCreateSpec universeCreateSpec = getUniverseCreateSpecV2Geo(true);
    AtomicReference<UniverseDefinitionTaskParams> paramsRef = new AtomicReference<>();
    setupUniverse(universeCreateSpec, paramsRef::set);
    UniverseDefinitionTaskParams v1CreateParams = paramsRef.get();
    validateUniverseCreateSpec(universeCreateSpec, v1CreateParams);
    assertThat(v1CreateParams.getPrimaryCluster().userIntent.isMulticloudSupport(), is(true));
    assertThat(v1CreateParams.getPrimaryCluster().userIntent.providerSpecifications, hasSize(1));
  }

  @Test
  public void testCreateUniverseWithRRV2() throws ApiException, IOException {
    AtomicReference<UniverseDefinitionTaskParams> paramsRef = new AtomicReference<>();
    UniverseCreateSpec universeCreateSpec = getUniverseCreateSpecWithRRV2();
    setupUniverse(universeCreateSpec, paramsRef::set);
    UniverseDefinitionTaskParams v1CreateParams = paramsRef.get();

    // validate that the Universe create params matches properties specified in the createSpec
    validateUniverseCreateSpec(universeCreateSpec, v1CreateParams);
  }

  @Test
  public void testCreateUniverseV2Geo() throws ApiException, IOException {
    AtomicReference<UniverseDefinitionTaskParams> paramsRef = new AtomicReference<>();
    UniverseCreateSpec universeCreateSpec = getUniverseCreateSpecV2Geo(false);
    setNewUIEnabled();
    setupUniverse(universeCreateSpec, paramsRef::set);
    UniverseDefinitionTaskParams v1CreateParams = paramsRef.get();

    // validate that the Universe create params matches properties specified in the createSpec
    validateUniverseCreateSpec(universeCreateSpec, v1CreateParams);

    assertThat(v1CreateParams.getPrimaryCluster().isGeoPartitioned(), is(true));

    UniverseApi api = new UniverseApi();
    com.yugabyte.yba.v2.client.models.Universe universeResp =
        api.getUniverse(customer.getUuid(), v1CreateParams.getUniverseUUID());

    assertThat(universeResp.getInfo().getClusters().get(0).getGeoPartitioned(), is(true));
  }

  @Test
  public void testDeleteUniverseV2() throws ApiException {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.DestroyUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(DestroyUniverse.Params.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getId());
    // Add the cloud info into the universe.
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
          UniverseDefinitionTaskParams.UserIntent userIntent =
              new UniverseDefinitionTaskParams.UserIntent();
          userIntent.providerType = Common.CloudType.aws;
          universeDetails.upsertPrimaryCluster(userIntent, null, null);
          universe.setUniverseDetails(universeDetails);
        };
    // Save the updates to the universe.
    Universe.saveDetails(u.getUniverseUUID(), updater);

    UniverseApi api = new UniverseApi();
    YBATask deleteTask = api.deleteUniverse(customer.getUuid(), u.getUniverseUUID(), null);
    UUID taskUUID = deleteTask.getTaskUuid();
    assertThat(taskUUID, is(fakeTaskUUID));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  @Parameters({
    "true, true, false",
    "false, true, true",
    "true, false, false",
    "false, false, true",
    "null, true, false",
  })
  public void testDeleteUniverseWithParamsV2(
      Boolean isForceDelete, Boolean isDeleteBackups, Boolean isDeleteAssociatedCerts)
      throws ApiException {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.DestroyUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(DestroyUniverse.Params.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getId());
    // Add the cloud info into the universe.
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
          UniverseDefinitionTaskParams.UserIntent userIntent =
              new UniverseDefinitionTaskParams.UserIntent();
          userIntent.providerType = Common.CloudType.aws;
          universeDetails.upsertPrimaryCluster(userIntent, null, null);
          universe.setUniverseDetails(universeDetails);
        };
    // Save the updates to the universe.
    Universe.saveDetails(u.getUniverseUUID(), updater);

    UniverseDeleteSpec universeDeleteSpec = new UniverseDeleteSpec();
    universeDeleteSpec
        .isForceDelete(isForceDelete)
        .isDeleteBackups(isDeleteBackups)
        .isDeleteAssociatedCerts(isDeleteAssociatedCerts);
    UniverseApi api = new UniverseApi();
    YBATask deleteTask =
        api.deleteUniverse(customer.getUuid(), u.getUniverseUUID(), universeDeleteSpec);
    UUID taskUUID = deleteTask.getTaskUuid();
    assertThat(taskUUID, is(fakeTaskUUID));
    assertAuditEntry(1, customer.getUuid());
    ArgumentCaptor<DestroyUniverse.Params> destroyParams =
        ArgumentCaptor.forClass(DestroyUniverse.Params.class);
    verify(mockCommissioner).submit(eq(TaskType.DestroyUniverse), destroyParams.capture());
    assertThat(destroyParams.getValue().isForceDelete, is(isForceDelete));
    assertThat(destroyParams.getValue().isDeleteBackups, is(isDeleteBackups));
    assertThat(destroyParams.getValue().isDeleteAssociatedCerts, is(isDeleteAssociatedCerts));
  }

  @Test
  public void testGetResizeOptions() throws ApiException, IOException {
    Universe universe =
        createFromConfig(
            Provider.getOrBadRequest(providerUuid),
            "Existing",
            "r1-z1r1-3-2;r1-z2r1-3-2;r1-z3r1-3-1");
    UUID uUUID = universe.getUniverseUUID();
    UniverseApi api = new UniverseApi();
    com.yugabyte.yba.v2.client.models.Universe universeResp =
        api.getUniverse(customer.getUuid(), uUUID);
    ClusterSpec clusterSpec = universeResp.getSpec().getClusters().get(0);
    String newInstanceType = "c4.xlarge";
    InstanceType i =
        InstanceType.upsert(
            providerUuid, newInstanceType, 10, 5.5, new InstanceType.InstanceTypeDetails());
    ClusterNodeSpec nodeSpec = universeResp.getSpec().getClusters().get(0).getNodeSpec();
    nodeSpec.getStorageSpec().volumeSize(500000);

    CheckResizeOptionsSpec spec =
        new CheckResizeOptionsSpec().nodeSpec(nodeSpec).clusterUuid(clusterSpec.getUuid());

    CheckResizeOptionsResp checkResizeOptionsResp =
        api.checkResizeOptions(customer.getUuid(), uUUID, spec);
    assertThat(
        new HashSet<>(checkResizeOptionsResp.getOptions()),
        is(Set.of(ResizeUpdateOption.SMART_RESIZE_NON_RESTART, ResizeUpdateOption.FULL_MOVE)));

    nodeSpec.instanceType(newInstanceType);
    checkResizeOptionsResp = api.checkResizeOptions(customer.getUuid(), uUUID, spec);
    assertThat(
        new HashSet<>(checkResizeOptionsResp.getOptions()),
        is(Set.of(ResizeUpdateOption.FULL_MOVE, ResizeUpdateOption.SMART_RESIZE)));

    nodeSpec.getStorageSpec().numVolumes(100);
    checkResizeOptionsResp = api.checkResizeOptions(customer.getUuid(), uUUID, spec);
    assertThat(
        new HashSet<>(checkResizeOptionsResp.getOptions()),
        is(Set.of(ResizeUpdateOption.FULL_MOVE)));
  }

  @Test
  public void testGetResizeOptionsMultiprovider() throws ApiException, IOException {
    setNewUIEnabled();
    UUID uUUID = setupUniverse(true);
    UniverseApi api = new UniverseApi();
    com.yugabyte.yba.v2.client.models.Universe universeResp =
        api.getUniverse(customer.getUuid(), uUUID);
    ClusterSpec clusterSpec = universeResp.getSpec().getClusters().get(0);
    String newInstanceType = "c4.xlarge";
    InstanceType.upsert(
        providerUuid, newInstanceType, 10, 5.5, new InstanceType.InstanceTypeDetails());

    PerProviderResizeNodesSpec providerNodesSpec =
        buildPerProviderResizeNodesSpec(providerUuid, ApiUtils.UTIL_INST_TYPE, 500000);

    CheckResizeOptionsSpec spec =
        new CheckResizeOptionsSpec()
            .clusterUuid(clusterSpec.getUuid())
            .providerNodesSpecs(List.of(providerNodesSpec));

    CheckResizeOptionsResp checkResizeOptionsResp =
        api.checkResizeOptions(customer.getUuid(), uUUID, spec);
    assertThat(
        new HashSet<>(checkResizeOptionsResp.getOptions()),
        is(Set.of(ResizeUpdateOption.SMART_RESIZE_NON_RESTART, ResizeUpdateOption.FULL_MOVE)));

    providerNodesSpec.getNodesSpec().getTserverSpecification().instanceType(newInstanceType);
    checkResizeOptionsResp = api.checkResizeOptions(customer.getUuid(), uUUID, spec);
    assertThat(
        new HashSet<>(checkResizeOptionsResp.getOptions()),
        is(Set.of(ResizeUpdateOption.FULL_MOVE, ResizeUpdateOption.SMART_RESIZE)));
  }

  @Test
  public void testGetResizeOptionsProviderNodesSpecsRequiresMulticloud()
      throws ApiException, IOException {
    UUID uUUID = setupUniverse(false);
    UniverseApi api = new UniverseApi();
    com.yugabyte.yba.v2.client.models.Universe universeResp =
        api.getUniverse(customer.getUuid(), uUUID);
    ClusterSpec clusterSpec = universeResp.getSpec().getClusters().get(0);

    CheckResizeOptionsSpec spec =
        new CheckResizeOptionsSpec()
            .clusterUuid(clusterSpec.getUuid())
            .providerNodesSpecs(
                List.of(
                    buildPerProviderResizeNodesSpec(
                        providerUuid, ApiUtils.UTIL_INST_TYPE, 500000)));

    ApiException ex =
        assertThrows(
            ApiException.class, () -> api.checkResizeOptions(customer.getUuid(), uUUID, spec));
    assertEquals(400, ex.getCode());
    assertThat(ex.getResponseBody(), containsString("provider_nodes_specs"));
  }

  @Test
  public void testGetResizeOptionsK8s() throws ApiException, IOException {
    // Kubernetes universes never do a smart resize; hardware/volume edits are applied by
    // recreating the StatefulSet pods. getUpdateOptions reports this as UPDATE (which is not a
    // ResizeUpdateOption), so the v2 endpoint must surface it as FULL_MOVE.
    Provider k8sProvider = ModelFactory.newProvider(customer, Common.CloudType.kubernetes);
    k8sProvider.setConfigMap(Map.of("KUBECONFIG", "foo"));
    k8sProvider.save();
    Universe universe =
        createFromConfig(k8sProvider, "ExistingK8s", "r1-z1r1-3-2;r1-z2r1-3-2;r1-z3r1-3-1");
    UUID uUUID = universe.getUniverseUUID();
    UniverseApi api = new UniverseApi();
    com.yugabyte.yba.v2.client.models.Universe universeResp =
        api.getUniverse(customer.getUuid(), uUUID);
    ClusterSpec clusterSpec = universeResp.getSpec().getClusters().get(0);
    ClusterNodeSpec nodeSpec = clusterSpec.getNodeSpec();
    // Volume expansion is classified by getUpdateOptions as a (k8s) UPDATE.
    nodeSpec.getStorageSpec().volumeSize(nodeSpec.getStorageSpec().getVolumeSize() + 100);

    CheckResizeOptionsSpec spec =
        new CheckResizeOptionsSpec().nodeSpec(nodeSpec).clusterUuid(clusterSpec.getUuid());

    CheckResizeOptionsResp checkResizeOptionsResp =
        api.checkResizeOptions(customer.getUuid(), uUUID, spec);
    assertThat(
        new HashSet<>(checkResizeOptionsResp.getOptions()),
        is(Set.of(ResizeUpdateOption.FULL_MOVE)));
  }
}
