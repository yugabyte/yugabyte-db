// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers.handlers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ProviderInitializer;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.AZOverrides;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PerProcessDetails;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ProviderSpecification;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UpdateOptions;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntentOverrides;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.AvailabilityZoneDetails;
import com.yugabyte.yw.models.AvailabilityZoneDetails.AZCloudInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.provider.region.KubernetesRegionInfo;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;

@RunWith(JUnitParamsRunner.class)
public class UniverseCRUDHandlerTest extends FakeDBApplication {

  private static final String MASTER_FIELDS_ERROR =
      "masterDeviceInfo and masterInstanceType can only be set when dedicated nodes for "
          + "master and tserver are selected.";

  private Customer customer;
  private UniverseCRUDHandler universeCRUDHandler;
  private Provider provider;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.awsProvider(customer);
    universeCRUDHandler = app.injector().instanceOf(UniverseCRUDHandler.class);
  }

  @Parameters({
    "enableYSQL",
    "enableYSQLAuth",
    "enableYCQL",
    "enableYCQLAuth",
    "enableYEDIS",
    "enableClientToNodeEncrypt",
    "enableNodeToNodeEncrypt",
    "assignPublicIP"
  })
  @Test
  public void testReadonlyClusterConsistency(String paramName) throws NoSuchFieldException {
    Field field = UniverseDefinitionTaskParams.UserIntent.class.getDeclaredField(paramName);
    if (field.getType().equals(boolean.class) || field.getType().equals(Boolean.class)) {
      for (boolean primaryVal : Arrays.asList(true, false)) {
        for (boolean readonlyVal : Arrays.asList(true, false)) {
          try {
            UniverseDefinitionTaskParams.UserIntent primary = testIntent();
            field.set(primary, primaryVal);
            UniverseDefinitionTaskParams.Cluster primaryCluster =
                new UniverseDefinitionTaskParams.Cluster(
                    UniverseDefinitionTaskParams.ClusterType.PRIMARY, primary);
            UniverseDefinitionTaskParams.UserIntent readonly = testIntent();
            field.set(readonly, readonlyVal);
            UniverseDefinitionTaskParams.Cluster readonlyCluster =
                new UniverseDefinitionTaskParams.Cluster(
                    UniverseDefinitionTaskParams.ClusterType.ASYNC, readonly);
            if (primaryVal == readonlyVal) {
              UniverseCRUDHandler.validateConsistency(primaryCluster, readonlyCluster);
            } else {
              assertThrows(
                  PlatformServiceException.class,
                  () -> UniverseCRUDHandler.validateConsistency(primaryCluster, readonlyCluster));
            }
          } catch (IllegalAccessException e) {
            throw new RuntimeException(e);
          }
        }
      }
    } else {
      throw new IllegalArgumentException("Unsupported type " + field.getType());
    }
  }

  // PLAT-23125: adding a read replica without otelCollectorEnabled in the request must not skip the
  // OTEL collector on the new nodes.
  @Test
  public void createClusterCarriesOtelCollectorEnabledFromUniverse() {
    Universe universe = ModelFactory.createFromConfig(provider, "otel-rr", "r1-az1-3-3");
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(), u -> u.getUniverseDetails().otelCollectorEnabled = true);
    UniverseDefinitionTaskParams.UserIntent rrIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    rrIntent.numNodes = 1;
    rrIntent.replicationFactor = 1;
    UniverseDefinitionTaskParams.Cluster asyncCluster =
        new UniverseDefinitionTaskParams.Cluster(
            UniverseDefinitionTaskParams.ClusterType.ASYNC, rrIntent);
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.clusters =
        new ArrayList<>(
            Arrays.asList(universe.getUniverseDetails().getPrimaryCluster(), asyncCluster));
    taskParams.nodeDetailsSet = universe.getUniverseDetails().nodeDetailsSet;
    taskParams.expectedUniverseVersion = -1;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams,
        customer.getId(),
        asyncCluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.CREATE);
    taskParams.clusters =
        new ArrayList<>(Collections.singletonList(taskParams.getClusterByUuid(asyncCluster.uuid)));
    taskParams.nodeDetailsSet =
        taskParams.nodeDetailsSet.stream()
            .filter(n -> n.isInPlacement(asyncCluster.uuid))
            .collect(Collectors.toSet());
    // As sent by a client that omits the field.
    taskParams.otelCollectorEnabled = false;
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(FakeDBApplication.buildTaskInfo(null, TaskType.ReadOnlyClusterCreate));

    universeCRUDHandler.createCluster(customer, universe, taskParams);

    ArgumentCaptor<UniverseDefinitionTaskParams> captor =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    verify(mockCommissioner).submit(any(TaskType.class), captor.capture());
    assertTrue(captor.getValue().otelCollectorEnabled);
  }

  @Test
  public void updatePrimaryIncorrectReplicasFailTest() {
    Universe universe = ModelFactory.createFromConfig(provider, "ahaha", "r1-az1-3-3;r2-az2-2-2");

    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    // This will make zone with 2 nodes have 3 replicas and vica-versa.
    primaryCluster
        .placementInfo
        .azStream()
        .forEach(az -> az.replicationFactor = 5 - az.replicationFactor);

    IllegalStateException ex =
        assertThrows(
            IllegalStateException.class,
            () ->
                universeCRUDHandler.update(
                    customer,
                    Universe.getOrBadRequest(universe.getUniverseUUID()),
                    universe.getUniverseDetails()));
    assertTrue(
        ex.getLocalizedMessage(),
        ex.getMessage()
            .contains("Cannot have number of replicas 3 greater than the number of nodes 2"));
  }

  // Only the cross-cloud federation API changes federationConfigured on an existing cluster. An
  // edit whose request omits it must keep the stored value.
  @Test
  public void updateKeepsFederationConfiguredFromUniverse() {
    Universe universe = ModelFactory.createFromConfig(provider, "fed-edit", "r1-az1-3-3");
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u ->
                u.getUniverseDetails()
                    .getPrimaryCluster()
                    .userIntent
                    .setFederationConfigured(true));
    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    // An edit that adds a node, as sent by a client that omits the field.
    UniverseDefinitionTaskParams.Cluster primary = taskParams.getPrimaryCluster();
    primary.userIntent.numNodes += 1;
    primary.userIntent.setFederationConfigured(false);
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams,
        customer.getId(),
        primary.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse));

    universeCRUDHandler.update(
        customer, Universe.getOrBadRequest(universe.getUniverseUUID()), taskParams);

    ArgumentCaptor<UniverseDefinitionTaskParams> captor =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    verify(mockCommissioner).submit(any(TaskType.class), captor.capture());
    assertTrue(captor.getValue().getPrimaryCluster().userIntent.isFederationConfigured());
  }

  private UniverseDefinitionTaskParams.UserIntent testIntent() {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 1;
    userIntent.replicationFactor = 1;
    userIntent.ybSoftwareVersion = "yb-version";
    return userIntent;
  }

  @Test
  public void checkInstanceTypeConsistency_passWhenNodesMatchIntent() {
    Universe u = ModelFactory.createUniverse(customer.getId());
    u = ModelFactory.addNodesToUniverse(u.getUniverseUUID(), 3);
    UUID primaryUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
    Universe.saveDetails(
        u.getUniverseUUID(),
        univ -> {
          UserIntent intent = univ.getUniverseDetails().getPrimaryCluster().userIntent;
          TestUtils.updateInstanceType(intent, "c5.4xlarge");
          for (NodeDetails n : univ.getUniverseDetails().getNodesInCluster(primaryUuid)) {
            n.cloudInfo.instance_type = "c5.4xlarge";
          }
          univ.setUniverseDetails(univ.getUniverseDetails());
        },
        false);
    u = Universe.getOrBadRequest(u.getUniverseUUID());
    UniverseCRUDHandler.checkInstanceTypeConsistency(u);
  }

  @Test
  public void checkInstanceTypeConsistency_failsOnDrift() {
    Universe u = ModelFactory.createUniverse(customer.getId());
    u = ModelFactory.addNodesToUniverse(u.getUniverseUUID(), 3);
    final UUID universeUuid = u.getUniverseUUID();
    UUID primaryUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
    Universe.saveDetails(
        universeUuid,
        univ -> {
          UserIntent intent = univ.getUniverseDetails().getPrimaryCluster().userIntent;
          TestUtils.updateInstanceType(intent, "c5.4xlarge");
          List<NodeDetails> nodes =
              univ.getUniverseDetails().getNodesInCluster(primaryUuid).stream()
                  .collect(Collectors.toList());
          nodes.get(0).cloudInfo.instance_type = "c5.4xlarge";
          nodes.get(1).cloudInfo.instance_type = "c5.9xlarge";
          nodes.get(2).cloudInfo.instance_type = "c5.9xlarge";
          nodes.get(0).nodeName = "host-n0";
          nodes.get(1).nodeName = "host-n1";
          nodes.get(2).nodeName = "host-n2";
          univ.setUniverseDetails(univ.getUniverseDetails());
        },
        false);
    final Universe universeAfterSave = Universe.getOrBadRequest(universeUuid);
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> UniverseCRUDHandler.checkInstanceTypeConsistency(universeAfterSave));
    assertEquals(BAD_REQUEST, ex.getHttpStatus());
    assertTrue(ex.getUserVisibleMessage().contains("host-n1"));
    assertTrue(ex.getUserVisibleMessage().contains("c5.9xlarge"));
    assertTrue(ex.getUserVisibleMessage().contains("c5.4xlarge"));
  }

  @Test
  public void checkInstanceTypeConsistency_passWithAzOverride() {
    Universe u = ModelFactory.createUniverse(customer.getId());
    u = ModelFactory.addNodesToUniverse(u.getUniverseUUID(), 2);
    UUID primaryUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
    UUID azUuid = UUID.randomUUID();
    Universe.saveDetails(
        u.getUniverseUUID(),
        univ -> {
          Cluster primary = univ.getUniverseDetails().getPrimaryCluster();
          TestUtils.updateInstanceType(primary.userIntent, "c5.4xlarge");
          UserIntentOverrides overrides = new UserIntentOverrides();
          AZOverrides azOverrides = new AZOverrides();
          azOverrides.setInstanceType("c5.9xlarge");
          overrides.setAzOverrides(new HashMap<>(Map.of(azUuid, azOverrides)));
          primary.userIntent.setUserIntentOverrides(overrides);
          for (NodeDetails n : univ.getUniverseDetails().getNodesInCluster(primaryUuid)) {
            n.azUuid = azUuid;
            n.cloudInfo.instance_type = "c5.9xlarge";
          }
          univ.setUniverseDetails(univ.getUniverseDetails());
        },
        false);
    u = Universe.getOrBadRequest(u.getUniverseUUID());
    UniverseCRUDHandler.checkInstanceTypeConsistency(u);
  }

  @Test
  public void checkInstanceTypeConsistency_failsWhenNodeDoesNotMatchAzOverride() {
    Universe u = ModelFactory.createUniverse(customer.getId());
    u = ModelFactory.addNodesToUniverse(u.getUniverseUUID(), 1);
    final UUID universeUuid = u.getUniverseUUID();
    UUID primaryUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
    UUID azUuid = UUID.randomUUID();
    Universe.saveDetails(
        universeUuid,
        univ -> {
          Cluster primary = univ.getUniverseDetails().getPrimaryCluster();
          TestUtils.updateInstanceType(primary.userIntent, "c5.4xlarge");
          UserIntentOverrides overrides = new UserIntentOverrides();
          AZOverrides azOverrides = new AZOverrides();
          azOverrides.setInstanceType("c5.9xlarge");
          overrides.setAzOverrides(new HashMap<>(Map.of(azUuid, azOverrides)));
          primary.userIntent.setUserIntentOverrides(overrides);
          NodeDetails n =
              univ.getUniverseDetails().getNodesInCluster(primaryUuid).iterator().next();
          n.azUuid = azUuid;
          n.nodeName = "host-n0";
          n.cloudInfo.instance_type = "m5.large";
          univ.setUniverseDetails(univ.getUniverseDetails());
        },
        false);
    final Universe universeAfterSave = Universe.getOrBadRequest(universeUuid);
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> UniverseCRUDHandler.checkInstanceTypeConsistency(universeAfterSave));
    assertEquals(BAD_REQUEST, ex.getHttpStatus());
    assertTrue(ex.getUserVisibleMessage().contains("m5.large"));
    assertTrue(ex.getUserVisibleMessage().contains("c5.9xlarge"));
  }

  @Test
  public void checkInstanceTypeConsistency_passDedicatedMaster() {
    Universe u = ModelFactory.createUniverse(customer.getId());
    u = ModelFactory.addNodesToUniverse(u.getUniverseUUID(), 2);
    UUID primaryUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
    List<NodeDetails> nodes =
        u.getUniverseDetails().getNodesInCluster(primaryUuid).stream().collect(Collectors.toList());
    Universe.saveDetails(
        u.getUniverseUUID(),
        univ -> {
          UserIntent intent = univ.getUniverseDetails().getPrimaryCluster().userIntent;
          TestUtils.updateInstanceType(intent, "c5.4xlarge");
          TestUtils.updateInstanceType(intent, ServerType.MASTER, "m5.2xlarge");
          intent.dedicatedNodes = true;
          NodeDetails master = nodes.get(0);
          master.nodeName = "host-master";
          master.isMaster = true;
          master.dedicatedTo = ServerType.MASTER;
          master.cloudInfo.instance_type = "m5.2xlarge";
          NodeDetails ts = nodes.get(1);
          ts.nodeName = "host-ts";
          ts.isTserver = true;
          ts.cloudInfo.instance_type = "c5.4xlarge";
          univ.setUniverseDetails(univ.getUniverseDetails());
        },
        false);
    u = Universe.getOrBadRequest(u.getUniverseUUID());
    UniverseCRUDHandler.checkInstanceTypeConsistency(u);
  }

  @Test
  public void checkInstanceTypeConsistency_skipsKubernetes() {
    ModelFactory.newProvider(customer, Common.CloudType.kubernetes);
    Universe u =
        ModelFactory.createUniverse(
            "k8s-u", UUID.randomUUID(), customer.getId(), Common.CloudType.kubernetes);
    u = ModelFactory.addNodesToUniverse(u.getUniverseUUID(), 2);
    UUID primaryUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
    Universe.saveDetails(
        u.getUniverseUUID(),
        univ -> {
          UserIntent intent = univ.getUniverseDetails().getPrimaryCluster().userIntent;
          TestUtils.updateInstanceType(intent, "small");

          for (NodeDetails n : univ.getUniverseDetails().getNodesInCluster(primaryUuid)) {
            n.cloudInfo.instance_type = "huge";
          }
          univ.setUniverseDetails(univ.getUniverseDetails());
        },
        false);
    u = Universe.getOrBadRequest(u.getUniverseUUID());
    UniverseCRUDHandler.checkInstanceTypeConsistency(u);
  }

  @Parameters({
    // enableYSQL, enableYCQL, skipYcqlPrecheck, expectError, expectedFragment
    "true,  false, false, false, ",
    "true,  false, true,  false, ",
    "true,  true,  false, true,  YCQL API should be disabled",
    "true,  true,  true,  false, ",
    "false, false, false, true,  YSQL API should be enabled",
    "false, false, true,  true,  YSQL API should be enabled",
    "false, true,  false, true,  YSQL API should be enabled",
    "false, true,  true,  true,  YSQL API should be enabled",
  })
  @Test
  public void validateMultiTenancyApiConfig_truthTable(
      boolean enableYSQL,
      boolean enableYCQL,
      boolean skipYcqlPrecheck,
      boolean expectError,
      String expectedFragment) {
    if (!expectError) {
      UniverseCRUDHandler.validateMultiTenancyApiConfig(enableYSQL, enableYCQL, skipYcqlPrecheck);
      return;
    }
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () ->
                UniverseCRUDHandler.validateMultiTenancyApiConfig(
                    enableYSQL, enableYCQL, skipYcqlPrecheck));
    assertEquals(BAD_REQUEST, ex.getHttpStatus());
    assertTrue(
        "Expected message to contain '"
            + expectedFragment
            + "', got: "
            + ex.getUserVisibleMessage(),
        ex.getUserVisibleMessage().contains(expectedFragment));
  }

  @Test
  public void validateMultiTenancyApiConfig_skipYcqlPrecheckBypassesYcqlOn() {
    // The regression case: skipYcqlPrecheck=true with YSQL on and YCQL on must pass.
    UniverseCRUDHandler.validateMultiTenancyApiConfig(
        true /* enableYSQL */, true /* enableYCQL */, true /* skipYcqlPrecheck */);
  }

  @Test
  public void validateMultiTenancyApiConfig_ysqlOffReportsYsqlError() {
    // Even when skipYcqlPrecheck would bypass the YCQL check, YSQL-off must still fail,
    // and the error message must point at YSQL (not the legacy combined string).
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () ->
                UniverseCRUDHandler.validateMultiTenancyApiConfig(
                    false /* enableYSQL */, false /* enableYCQL */, true /* skipYcqlPrecheck */));
    assertEquals(BAD_REQUEST, ex.getHttpStatus());
    assertTrue(ex.getUserVisibleMessage().contains("YSQL API should be enabled"));
  }

  @Test
  public void configureRejectsMasterDeviceInfoWhenDedicatedNodesDisabled() {
    UniverseConfigureTaskParams taskParams = buildConfigureTaskParams(false, true, false);

    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> universeCRUDHandler.configure(customer, taskParams));

    assertEquals(BAD_REQUEST, ex.getHttpStatus());
    assertTrue(ex.getUserVisibleMessage().contains(MASTER_FIELDS_ERROR));
  }

  @Test
  public void configureRejectsMasterInstanceTypeWhenDedicatedNodesDisabled() {
    UniverseConfigureTaskParams taskParams = buildConfigureTaskParams(false, false, true);

    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> universeCRUDHandler.configure(customer, taskParams));

    assertEquals(BAD_REQUEST, ex.getHttpStatus());
    assertTrue(ex.getUserVisibleMessage().contains(MASTER_FIELDS_ERROR));
  }

  @Test
  public void checkInstanceTypeConsistency_skipsToBeAddedAndToBeRemoved() {
    Universe u = ModelFactory.createUniverse(customer.getId());
    u = ModelFactory.addNodesToUniverse(u.getUniverseUUID(), 1);
    UUID primaryUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
    Universe.saveDetails(
        u.getUniverseUUID(),
        univ -> {
          UserIntent intent = univ.getUniverseDetails().getPrimaryCluster().userIntent;
          TestUtils.updateInstanceType(intent, "c5.4xlarge");
          NodeDetails live =
              univ.getUniverseDetails().getNodesInCluster(primaryUuid).iterator().next();
          live.nodeName = "host-live";
          live.state = NodeState.Live;
          live.cloudInfo.instance_type = "c5.4xlarge";
          NodeDetails toAdd = new NodeDetails();
          toAdd.nodeName = "host-add";
          toAdd.state = NodeState.ToBeAdded;
          toAdd.placementUuid = primaryUuid;
          toAdd.cloudInfo = new CloudSpecificInfo();
          toAdd.cloudInfo.instance_type = "wrong";
          toAdd.azUuid = live.azUuid;
          univ.getUniverseDetails().nodeDetailsSet.add(toAdd);
          NodeDetails toRemove = new NodeDetails();
          toRemove.nodeName = "host-rem";
          toRemove.state = NodeState.ToBeRemoved;
          toRemove.placementUuid = primaryUuid;
          toRemove.cloudInfo = new CloudSpecificInfo();
          toRemove.cloudInfo.instance_type = "wrong";
          toRemove.azUuid = live.azUuid;
          univ.getUniverseDetails().nodeDetailsSet.add(toRemove);
          univ.setUniverseDetails(univ.getUniverseDetails());
        },
        false);
    u = Universe.getOrBadRequest(u.getUniverseUUID());
    UniverseCRUDHandler.checkInstanceTypeConsistency(u);
  }

  /*--- Tests for validateAndInitKubernetesCluster volume-override handling ---*/

  /**
   * Builds a Kubernetes AZ whose KubernetesRegionInfo carries the given storage class, so that
   * {@code CloudInfoInterface.fetchEnvVars(zone)} surfaces it as {@code STORAGE_CLASS} - the value
   * the default {@code applyVolumeChanges} fallback picks up.
   */
  private AvailabilityZone createK8sAZ(Region region, String code, String storageClass) {
    AvailabilityZone az =
        AvailabilityZone.createOrThrow(region, code, code.toUpperCase(), "subnet-" + code);
    az.setDetails(new AvailabilityZoneDetails());
    az.getDetails().setCloudInfo(new AZCloudInfo());
    KubernetesRegionInfo k8sInfo = new KubernetesRegionInfo();
    if (storageClass != null) {
      k8sInfo.setKubernetesStorageClass(storageClass);
    }
    az.getDetails().getCloudInfo().setKubernetes(k8sInfo);
    az.save();
    return az;
  }

  private UserIntent buildK8sUserIntent(Provider k8sProvider) {
    UserIntent userIntent = new UserIntent();
    userIntent.universeName = "k8s-univ";
    userIntent.provider = k8sProvider.getUuid().toString();
    userIntent.providerType = Common.CloudType.kubernetes;
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.volumeSize = 100;
    userIntent.deviceInfo.numVolumes = 1;
    userIntent.masterDeviceInfo = new DeviceInfo();
    userIntent.masterDeviceInfo.volumeSize = 50;
    userIntent.masterDeviceInfo.numVolumes = 1;
    return userIntent;
  }

  /**
   * Seeds a single-AZ TSERVER per-AZ override onto the userIntent, as the operator would compute.
   */
  private void seedTserverAZOverride(
      UserIntent userIntent, UUID azUuid, int volumeSize, String storageClass) {
    UserIntentOverrides overrides = new UserIntentOverrides();
    Map<UUID, AZOverrides> azMap = new HashMap<>();
    AZOverrides azOv = new AZOverrides();
    Map<ServerType, PerProcessDetails> perProcess = new HashMap<>();
    PerProcessDetails details = new PerProcessDetails();
    DeviceInfo di = new DeviceInfo();
    di.volumeSize = volumeSize;
    di.storageClass = storageClass;
    details.setDeviceInfo(di);
    perProcess.put(ServerType.TSERVER, details);
    azOv.setPerProcess(perProcess);
    azMap.put(azUuid, azOv);
    overrides.setAzOverrides(azMap);
    userIntent.setUserIntentOverrides(overrides);
  }

  private UniverseConfigureTaskParams buildPrimaryK8sTaskParams(
      UserIntent userIntent, boolean operatorControlled, AvailabilityZone... zones) {
    Cluster cluster = new Cluster(ClusterType.PRIMARY, userIntent);
    Map<UUID, Integer> azToNodes = new HashMap<>();
    for (AvailabilityZone z : zones) {
      azToNodes.put(z.getUuid(), 1);
    }
    cluster.placementInfo = ModelFactory.constructPlacementInfoObject(azToNodes);
    UniverseConfigureTaskParams taskParams = new UniverseConfigureTaskParams();
    taskParams.currentClusterType = ClusterType.PRIMARY;
    taskParams.clusterOperation = ClusterOperationType.CREATE;
    taskParams.isKubernetesOperatorControlled = operatorControlled;
    taskParams.clusters.add(cluster);
    return taskParams;
  }

  // PLAT-21725: volume/userIntentOverrides handling was moved out of
  // validateAndInitKubernetesCluster - it now happens later in createUniverse, once the placement
  // is finalized and guarded so operator universes are not touched. This test locks in that
  // validateAndInitKubernetesCluster does NOT compute or clobber userIntentOverrides for an
  // operator-controlled universe: the reconciler-computed override for az-1 survives and no
  // provider-storage-class override is materialized for az-2.
  @Test
  public void validateAndInitKubernetesClusterOperatorPreservesUserIntentOverrides() {
    Provider k8sProvider = ModelFactory.kubernetesProvider(customer);
    Region region = Region.create(k8sProvider, "region-1", "Region 1", "default-image");
    AvailabilityZone az1 = createK8sAZ(region, "az-1", "provider-sc");
    AvailabilityZone az2 = createK8sAZ(region, "az-2", "provider-sc");

    UserIntent userIntent = buildK8sUserIntent(k8sProvider);
    // Operator pre-computed a per-AZ override for az-1 only.
    seedTserverAZOverride(userIntent, az1.getUuid(), 250, "az1-special-sc");

    UniverseConfigureTaskParams taskParams =
        buildPrimaryK8sTaskParams(userIntent, true /* operatorControlled */, az1, az2);

    universeCRUDHandler.validateAndInitKubernetesCluster(
        taskParams.getPrimaryCluster(), taskParams);

    UserIntentOverrides result = taskParams.getPrimaryCluster().userIntent.getUserIntentOverrides();
    assertNotNull(result);
    Map<UUID, AZOverrides> azOverrides = result.getAzOverrides();
    assertNotNull(azOverrides);
    // Only az-1 must be present; az-2 must NOT have been added from provider storage class.
    assertEquals(Set.of(az1.getUuid()), azOverrides.keySet());
    DeviceInfo az1Di =
        azOverrides.get(az1.getUuid()).getPerProcess().get(ServerType.TSERVER).getDeviceInfo();
    assertEquals(250, az1Di.volumeSize.intValue());
    assertEquals("az1-special-sc", az1Di.storageClass);
    assertNull(azOverrides.get(az2.getUuid()));
  }

  // PLAT-21725: validateAndInitKubernetesCluster must not compute volume overrides for the regular
  // (non-operator) k8s path either - that is done by the single applyVolumeChanges call later in
  // createUniverse after the placement is finalized. Even though az-2 has a provider storage class,
  // validateAndInitKubernetesCluster leaves userIntentOverrides untouched (az-2 is not added).
  @Test
  public void validateAndInitKubernetesClusterNonOperatorDoesNotComputeVolumeOverrides() {
    Provider k8sProvider = ModelFactory.kubernetesProvider(customer);
    Region region = Region.create(k8sProvider, "region-1", "Region 1", "default-image");
    AvailabilityZone az1 = createK8sAZ(region, "az-1", "provider-sc");
    AvailabilityZone az2 = createK8sAZ(region, "az-2", "provider-sc");

    UserIntent userIntent = buildK8sUserIntent(k8sProvider);
    seedTserverAZOverride(userIntent, az1.getUuid(), 250, "az1-special-sc");

    UniverseConfigureTaskParams taskParams =
        buildPrimaryK8sTaskParams(userIntent, false /* operatorControlled */, az1, az2);

    universeCRUDHandler.validateAndInitKubernetesCluster(
        taskParams.getPrimaryCluster(), taskParams);

    UserIntentOverrides result = taskParams.getPrimaryCluster().userIntent.getUserIntentOverrides();
    assertNotNull(result);
    Map<UUID, AZOverrides> azOverrides = result.getAzOverrides();
    assertNotNull(azOverrides);
    // az-2 must NOT have been added here; volume-override computation happens later.
    assertEquals(Set.of(az1.getUuid()), azOverrides.keySet());
    assertNull(azOverrides.get(az2.getUuid()));
  }

  // PLAT-21736: use_memory_defaults_optimized_for_ysql must be set on both Master and TServer,
  // since the two processes rely on agreeing on this flag to negotiate available memory.
  @Test
  public void maybeSetNewInstallGflags_setsMemoryFlagOnBothMasterAndTserver() {
    Universe universe = ModelFactory.createUniverse(customer.getId());
    Cluster primaryCluster = newInstallGflagsCluster("2024.2.1.0", false, true);

    universeCRUDHandler.maybeSetNewInstallGflags(customer, universe, primaryCluster);

    Map<String, String> masterFlags = universe.getNewInstallGFlags(ServerType.MASTER);
    Map<String, String> tserverFlags = universe.getNewInstallGFlags(ServerType.TSERVER);
    assertEquals("true", masterFlags.get("use_memory_defaults_optimized_for_ysql"));
    assertEquals("true", tserverFlags.get("use_memory_defaults_optimized_for_ysql"));
    // Sanity check the sibling flags from the same eligibility bucket are still master-only.
    assertEquals("true", masterFlags.get("enforce_tablet_replica_limits"));
    assertEquals("true", masterFlags.get("split_respects_tablet_replica_limits"));
    assertNull(universe.getConfig().get(Universe.K8S_APPLY_NEW_INSTALL_GFLAGS));
  }

  @Test
  public void maybeSetNewInstallGflags_k8sGetsYsqlFlagsAndAppliesThem() {
    Universe universe = ModelFactory.createUniverse(customer.getId());
    Cluster primaryCluster = newInstallGflagsCluster("2025.2.0.0", false, true);
    primaryCluster.userIntent.providerType = Common.CloudType.kubernetes;

    universeCRUDHandler.maybeSetNewInstallGflags(customer, universe, primaryCluster);

    for (ServerType serverType : List.of(ServerType.MASTER, ServerType.TSERVER)) {
      Map<String, String> flags = universe.getNewInstallGFlags(serverType);
      assertEquals("true", flags.get("yb_enable_read_committed_isolation"));
      assertEquals("yb_enable_cbo=on", flags.get("ysql_pg_conf_csv"));
      assertTrue(!flags.containsKey("use_memory_defaults_optimized_for_ysql"));
    }
    assertEquals("true", universe.getConfig().get(Universe.K8S_APPLY_NEW_INSTALL_GFLAGS));
  }

  @Test
  public void maybeSetNewInstallGflags_noMemoryFlagWhenYsqlDisabled() {
    Universe universe = ModelFactory.createUniverse(customer.getId());
    Cluster primaryCluster = newInstallGflagsCluster("2024.2.1.0", false, false);

    universeCRUDHandler.maybeSetNewInstallGflags(customer, universe, primaryCluster);

    Map<String, String> masterFlags = universe.getNewInstallGFlags(ServerType.MASTER);
    Map<String, String> tserverFlags = universe.getNewInstallGFlags(ServerType.TSERVER);
    assertTrue(!masterFlags.containsKey("use_memory_defaults_optimized_for_ysql"));
    assertTrue(!tserverFlags.containsKey("use_memory_defaults_optimized_for_ysql"));
    // Non-memory flags in this bucket are unaffected by enableYSQL.
    assertEquals("true", masterFlags.get("enforce_tablet_replica_limits"));
  }

  @Test
  public void maybeSetNewInstallGflags_noMemoryFlagBelowVersionThreshold() {
    Universe universe = ModelFactory.createUniverse(customer.getId());
    Cluster primaryCluster = newInstallGflagsCluster("2024.1.0.0", false, true);

    universeCRUDHandler.maybeSetNewInstallGflags(customer, universe, primaryCluster);

    assertTrue(universe.getNewInstallGFlags(ServerType.MASTER).isEmpty());
    assertTrue(universe.getNewInstallGFlags(ServerType.TSERVER).isEmpty());
  }

  @Test
  public void maybeSetNewInstallGflags_noMemoryFlagForDedicatedNodes() {
    Universe universe = ModelFactory.createUniverse(customer.getId());
    Cluster primaryCluster = newInstallGflagsCluster("2024.2.1.0", true, true);

    universeCRUDHandler.maybeSetNewInstallGflags(customer, universe, primaryCluster);

    assertTrue(
        !universe
            .getNewInstallGFlags(ServerType.MASTER)
            .containsKey("use_memory_defaults_optimized_for_ysql"));
    assertTrue(
        !universe
            .getNewInstallGFlags(ServerType.TSERVER)
            .containsKey("use_memory_defaults_optimized_for_ysql"));
  }

  private Cluster newInstallGflagsCluster(
      String ybSoftwareVersion, boolean dedicatedNodes, boolean enableYSQL) {
    UserIntent userIntent = testIntent();
    userIntent.ybSoftwareVersion = ybSoftwareVersion;
    userIntent.dedicatedNodes = dedicatedNodes;
    userIntent.enableYSQL = enableYSQL;
    userIntent.providerType = Common.CloudType.aws;
    return new Cluster(ClusterType.PRIMARY, userIntent);
  }

  private UniverseConfigureTaskParams buildConfigureTaskParams(
      boolean dedicatedNodes, boolean setMasterDeviceInfo, boolean setMasterInstanceType) {
    UserIntent userIntent = new UserIntent();
    userIntent.dedicatedNodes = dedicatedNodes;
    ProviderInitializer pi =
        TestUtils.getProviderInitializerForTests(userIntent, UUID.randomUUID())
            .setDeviceInfo(ApiUtils.getDummyDeviceInfo(1, 100));
    if (setMasterDeviceInfo) {
      pi.setMasterDeviceInfo(ApiUtils.getDummyDeviceInfo(1, 50));
    }
    if (setMasterInstanceType) {
      pi.setMasterInstanceType("m5.large");
    }
    Cluster cluster = new Cluster(ClusterType.PRIMARY, userIntent);
    UniverseConfigureTaskParams taskParams = new UniverseConfigureTaskParams();
    taskParams.currentClusterType = ClusterType.PRIMARY;
    taskParams.clusterOperation = ClusterOperationType.EDIT;
    taskParams.clusters.add(cluster);
    return taskParams;
  }

  /*--- Tests for haveSameProviderIds ---*/

  private static final String PROVIDER_SPECS_CHANGED =
      "The list of provider specifications is changed";

  private Cluster legacyProviderCluster(UUID providerUUID) {
    UserIntent userIntent = testIntent();
    userIntent.provider = providerUUID.toString();
    userIntent.providerType = Common.CloudType.aws;
    return new Cluster(ClusterType.PRIMARY, userIntent);
  }

  private Cluster specProviderCluster(UUID... providerUUIDs) {
    UserIntent userIntent = testIntent();
    for (UUID providerUUID : providerUUIDs) {
      TestUtils.specificationProviderInitializer(userIntent, providerUUID)
          .setProviderType(Common.CloudType.aws)
          .setAccessCode("demo-access");
    }
    return new Cluster(ClusterType.PRIMARY, userIntent);
  }

  @Test
  public void haveSameProviderIds_legacySameProvider() {
    UUID providerUUID = UUID.randomUUID();
    assertTrue(
        UniverseCRUDHandler.haveSameProviderIds(
            legacyProviderCluster(providerUUID), legacyProviderCluster(providerUUID)));
  }

  @Test
  public void haveSameProviderIds_legacyDifferentProvider() {
    assertFalse(
        UniverseCRUDHandler.haveSameProviderIds(
            legacyProviderCluster(UUID.randomUUID()), legacyProviderCluster(UUID.randomUUID())));
  }

  @Test
  public void haveSameProviderIds_noProviders() {
    assertTrue(
        UniverseCRUDHandler.haveSameProviderIds(
            new Cluster(ClusterType.PRIMARY, testIntent()),
            new Cluster(ClusterType.PRIMARY, testIntent())));
  }

  @Test
  public void haveSameProviderIds_specsSameProvidersDifferentOrder() {
    UUID p1 = UUID.randomUUID();
    UUID p2 = UUID.randomUUID();
    assertTrue(
        UniverseCRUDHandler.haveSameProviderIds(
            specProviderCluster(p1, p2), specProviderCluster(p2, p1)));
  }

  @Test
  public void haveSameProviderIds_specsProviderAdded() {
    UUID p1 = UUID.randomUUID();
    assertFalse(
        UniverseCRUDHandler.haveSameProviderIds(
            specProviderCluster(p1, UUID.randomUUID()), specProviderCluster(p1)));
  }

  @Test
  public void haveSameProviderIds_specsProviderRemoved() {
    UUID p1 = UUID.randomUUID();
    assertFalse(
        UniverseCRUDHandler.haveSameProviderIds(
            specProviderCluster(p1), specProviderCluster(p1, UUID.randomUUID())));
  }

  @Test
  public void haveSameProviderIds_specsProviderReplaced() {
    UUID p1 = UUID.randomUUID();
    assertFalse(
        UniverseCRUDHandler.haveSameProviderIds(
            specProviderCluster(p1, UUID.randomUUID()),
            specProviderCluster(p1, UUID.randomUUID())));
  }

  // The full spec comparison used to treat any difference as a provider change. Only the set of
  // provider IDs matters now; the other fields are checked separately.
  @Test
  public void haveSameProviderIds_ignoresOtherSpecFields() {
    UUID p1 = UUID.randomUUID();
    UUID p2 = UUID.randomUUID();
    Cluster current = specProviderCluster(p1, p2);
    Cluster cluster = specProviderCluster(p1, p2);
    ProviderSpecification spec = cluster.userIntent.getProviderSpecification(p1);
    spec.setAccessKeyCode("other-access");
    spec.setInstanceTags(new HashMap<>(Map.of("key", "value")));
    spec.setImageBundleUUID(UUID.randomUUID());
    spec.setAwsInstanceProfile("arn:aws:iam::123456789012:instance-profile/other");
    spec.setHelmOverrides("foo: bar");
    cluster.userIntent.getProviderSpecification(p2).setProviderType(Common.CloudType.gcp);

    assertTrue(UniverseCRUDHandler.haveSameProviderIds(cluster, current));
  }

  @Test
  public void haveSameProviderIds_legacyAndSpecWithSameProvider() {
    UUID providerUUID = UUID.randomUUID();
    assertTrue(
        UniverseCRUDHandler.haveSameProviderIds(
            specProviderCluster(providerUUID), legacyProviderCluster(providerUUID)));
  }

  /*--- Tests for getUpdateOptions ---*/

  private static final String TAGS_CHANGED = "There are changes in tags";

  // Universe whose primary cluster uses a provider specification (multicloud-style intent).
  private Universe createSpecUniverse() {
    Universe universe = ModelFactory.createFromConfig(provider, "spec-u", "r1-az1-3-3");
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UserIntent intent = u.getUniverseDetails().getPrimaryCluster().userIntent;
              TestUtils.specificationProviderInitializer(intent, provider.getUuid())
                  .setProviderType(Common.CloudType.aws)
                  .setInstanceType(ApiUtils.UTIL_INST_TYPE)
                  .setDeviceInfo(ApiUtils.getDummyDeviceInfo(1, 100))
                  .setAccessCode("demo-access");
            });
    assertTrue(universe.getUniverseDetails().getPrimaryCluster().userIntent.isMulticloudSupport());
    return universe;
  }

  // A separate copy of the universe details to modify as the edit request.
  private UniverseDefinitionTaskParams editParams(Universe universe) {
    return Universe.getOrBadRequest(universe.getUniverseUUID()).getUniverseDetails();
  }

  // Changing a provider spec field that no other check covers (here the access key) must not be
  // reported as a provider change or force an UPDATE.
  @Test
  public void getUpdateOptions_specFieldChangeIsNotProviderChange() {
    Universe universe = createSpecUniverse();
    UniverseDefinitionTaskParams taskParams = editParams(universe);
    Cluster cluster = taskParams.getPrimaryCluster();
    cluster.userIntent.getProviderSpecification(provider.getUuid()).setAccessKeyCode("other");

    UniverseCRUDHandler.UpdateOptionsState state =
        UniverseCRUDHandler.getUpdateOptions(
            taskParams, ClusterOperationType.EDIT, cluster, universe);

    assertFalse(state.isHasResizeChanges());
    assertFalse(
        state.getResizeRestrictions().toString(),
        state.getResizeRestrictions().contains(PROVIDER_SPECS_CHANGED));
    assertFalse(
        state.getUpdateOptions().toString(),
        state.getUpdateOptions().contains(UpdateOptions.UPDATE));
  }

  // With nothing changed there are no resize changes and "Nothing changed" is no longer reported
  // as a restriction.
  @Test
  public void getUpdateOptions_noChanges() {
    Universe universe = createSpecUniverse();
    UniverseDefinitionTaskParams taskParams = editParams(universe);
    Cluster cluster = taskParams.getPrimaryCluster();

    UniverseCRUDHandler.UpdateOptionsState state =
        UniverseCRUDHandler.getUpdateOptions(
            taskParams, ClusterOperationType.EDIT, cluster, universe);

    assertFalse(state.isHasResizeChanges());
    assertTrue(state.getResizeRestrictions().toString(), state.getResizeRestrictions().isEmpty());
  }

  // Non-resize changes are reported as restrictions even when there is nothing to resize.
  @Test
  public void getUpdateOptions_nonResizeChangeWithoutResizeChanges() {
    Universe universe = createSpecUniverse();
    UniverseDefinitionTaskParams taskParams = editParams(universe);
    Cluster cluster = taskParams.getPrimaryCluster();
    TestUtils.specificationProviderInitializer(cluster.userIntent, provider.getUuid())
        .setInstanceTags(new HashMap<>(Map.of("key", "value")));

    UniverseCRUDHandler.UpdateOptionsState state =
        UniverseCRUDHandler.getUpdateOptions(
            taskParams, ClusterOperationType.EDIT, cluster, universe);

    assertFalse(state.isHasResizeChanges());
    assertEquals(Set.of(TAGS_CHANGED), state.getResizeRestrictions());
    assertTrue(
        state.getUpdateOptions().toString(),
        state.getUpdateOptions().contains(UpdateOptions.UPDATE));
  }

  // A resize-only change allows smart resize and is still flagged as a resize change.
  @Test
  public void getUpdateOptions_resizeOnlyChange() {
    Universe universe = createSpecUniverse();
    UniverseDefinitionTaskParams taskParams = editParams(universe);
    Cluster cluster = taskParams.getPrimaryCluster();
    TestUtils.specificationProviderInitializer(cluster.userIntent, provider.getUuid())
        .setCGroupSize(2048);

    UniverseCRUDHandler.UpdateOptionsState state =
        UniverseCRUDHandler.getUpdateOptions(
            taskParams, ClusterOperationType.EDIT, cluster, universe);

    assertTrue(state.isHasResizeChanges());
    assertTrue(state.getResizeRestrictions().toString(), state.getResizeRestrictions().isEmpty());
    assertTrue(
        state.getUpdateOptions().toString(),
        state.getUpdateOptions().contains(UpdateOptions.SMART_RESIZE_NON_RESTART));
  }

  // A resize change combined with a non-resize change: smart resize is blocked, the resize changes
  // are flagged and the blocking change is reported.
  @Test
  public void getUpdateOptions_resizeChangeBlockedByNonResizeChange() {
    Universe universe = createSpecUniverse();
    UniverseDefinitionTaskParams taskParams = editParams(universe);
    Cluster cluster = taskParams.getPrimaryCluster();
    TestUtils.specificationProviderInitializer(cluster.userIntent, provider.getUuid())
        .setCGroupSize(2048)
        .setInstanceTags(new HashMap<>(Map.of("key", "value")));

    UniverseCRUDHandler.UpdateOptionsState state =
        UniverseCRUDHandler.getUpdateOptions(
            taskParams, ClusterOperationType.EDIT, cluster, universe);

    assertTrue(state.isHasResizeChanges());
    assertEquals(Set.of(TAGS_CHANGED), state.getResizeRestrictions());
    assertFalse(
        state.getUpdateOptions().toString(),
        state.getUpdateOptions().contains(UpdateOptions.SMART_RESIZE)
            || state.getUpdateOptions().contains(UpdateOptions.SMART_RESIZE_NON_RESTART));
  }
}
