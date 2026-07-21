// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers.handlers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.AZOverrides;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PerProcessDetails;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntentOverrides;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.AvailabilityZoneDetails;
import com.yugabyte.yw.models.AvailabilityZoneDetails.AZCloudInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.provider.region.KubernetesRegionInfo;
import java.lang.reflect.Field;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

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

  private UniverseDefinitionTaskParams.UserIntent testIntent() {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 1;
    userIntent.replicationFactor = 1;
    userIntent.ybSoftwareVersion = "yb-version";
    return userIntent;
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

  private UniverseConfigureTaskParams buildConfigureTaskParams(
      boolean dedicatedNodes, boolean setMasterDeviceInfo, boolean setMasterInstanceType) {
    UserIntent userIntent = new UserIntent();
    userIntent.dedicatedNodes = dedicatedNodes;
    userIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    if (setMasterDeviceInfo) {
      userIntent.masterDeviceInfo = ApiUtils.getDummyDeviceInfo(1, 50);
    }
    if (setMasterInstanceType) {
      userIntent.masterInstanceType = "m5.large";
    }

    Cluster cluster = new Cluster(ClusterType.PRIMARY, userIntent);
    UniverseConfigureTaskParams taskParams = new UniverseConfigureTaskParams();
    taskParams.currentClusterType = ClusterType.PRIMARY;
    taskParams.clusterOperation = ClusterOperationType.EDIT;
    taskParams.clusters.add(cluster);
    return taskParams;
  }
}
