// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.AvailabilityZoneDetails;
import com.yugabyte.yw.models.AvailabilityZoneDetails.AZCloudInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.ProviderDetails.CloudInfo;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RegionDetails;
import com.yugabyte.yw.models.RegionDetails.RegionCloudInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import com.yugabyte.yw.models.helpers.provider.region.KubernetesRegionInfo;
import com.yugabyte.yw.models.migrations.V437;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;

@Slf4j
public class V437__Populate_DeviceInfo_StorageClassTest extends FakeDBApplication {

  private Customer customer;
  private Provider kubernetesProvider;
  private Region region1;
  private AvailabilityZone az1;
  private AvailabilityZone az2;
  private AvailabilityZone az3;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();

    // Create Kubernetes provider
    kubernetesProvider =
        Provider.create(
            customer.getUuid(),
            CloudType.kubernetes,
            "K8s Provider",
            new HashMap<String, String>());
    CloudInfo cloudInfo = new ProviderDetails.CloudInfo();
    cloudInfo.kubernetes = new KubernetesInfo();
    kubernetesProvider.getDetails().setCloudInfo(cloudInfo);
    kubernetesProvider.save();

    // Create region
    region1 = Region.create(kubernetesProvider, "region-1", "Region 1", "default-image");
    region1.setDetails(new RegionDetails());
    region1.getDetails().setCloudInfo(new RegionCloudInfo());
    region1.getDetails().getCloudInfo().kubernetes = new KubernetesRegionInfo();
    region1.save();

    // Create availability zones
    az1 = AvailabilityZone.createOrThrow(region1, "az-1", "AZ 1", "subnet-1");
    az1.setDetails(new AvailabilityZoneDetails());
    az1.getDetails().setCloudInfo(new AZCloudInfo());
    az1.getDetails().getCloudInfo().setKubernetes(new KubernetesRegionInfo());
    az1.save();
    az2 = AvailabilityZone.createOrThrow(region1, "az-2", "AZ 2", "subnet-2");
    az2.setDetails(new AvailabilityZoneDetails());
    az2.getDetails().setCloudInfo(new AZCloudInfo());
    az2.getDetails().getCloudInfo().setKubernetes(new KubernetesRegionInfo());
    az2.save();
    az3 = AvailabilityZone.createOrThrow(region1, "az-3", "AZ 3", "subnet-3");
    az3.setDetails(new AvailabilityZoneDetails());
    az3.getDetails().setCloudInfo(new AZCloudInfo());
    az3.getDetails().getCloudInfo().setKubernetes(new KubernetesRegionInfo());
    az3.save();
  }

  @Test
  public void testMigrationWithProviderStorageClass() {
    // Setup: Provider has STORAGE_CLASS
    kubernetesProvider
        .getDetails()
        .getCloudInfo()
        .getKubernetes()
        .setKubernetesStorageClass("provider-storage-class");
    kubernetesProvider.save();

    // Create universe without deviceInfo storageClass
    Universe universe = createTestUniverse("test-universe-1", null, null, null, null);

    // Run migration
    V437__Populate_DeviceInfo_StorageClass.migrateDeviceInfoStorageClass();

    // Verify: deviceInfo and masterDeviceInfo should have storageClass from provider
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    DeviceInfo deviceInfo = details.getPrimaryCluster().userIntent.deviceInfo;
    DeviceInfo masterDeviceInfo = details.getPrimaryCluster().userIntent.masterDeviceInfo;

    assertNotNull(deviceInfo);
    assertEquals("provider-storage-class", deviceInfo.storageClass);
    assertNotNull(masterDeviceInfo);
    assertEquals("provider-storage-class", masterDeviceInfo.storageClass);
  }

  @Test
  public void testMigrationWithUniverseOverrides() {
    // Setup: Universe has storage class in universeOverrides
    String universeOverrides =
        "storage:\n"
            + "  tserver:\n"
            + "    storageClass: universe-tserver-sc\n"
            + "    count: 3\n"
            + "    size: 150Gi\n"
            + "  master:\n"
            + "    storageClass: universe-master-sc\n"
            + "    count: 1\n"
            + "    size: 60Gi\n"
            + "gflags:\n"
            + "  tserver:\n"
            + "    test_flag: test_value";

    Universe universe = createTestUniverse("test-universe-2", universeOverrides, null, null, null);

    // Run migration
    V437__Populate_DeviceInfo_StorageClass.migrateDeviceInfoStorageClass();

    // Verify: deviceInfo should have storageClass from universeOverrides
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    log.info(universe.getUniverseDetailsJson());
    DeviceInfo deviceInfo = details.getPrimaryCluster().userIntent.deviceInfo;
    DeviceInfo masterDeviceInfo = details.getPrimaryCluster().userIntent.masterDeviceInfo;

    assertNotNull(deviceInfo);
    assertEquals("universe-tserver-sc", deviceInfo.storageClass);
    assertEquals(Integer.valueOf(3), deviceInfo.numVolumes);
    assertEquals(Integer.valueOf(150), deviceInfo.volumeSize);

    assertNotNull(masterDeviceInfo);
    assertEquals("universe-master-sc", masterDeviceInfo.storageClass);
    assertEquals(Integer.valueOf(1), masterDeviceInfo.numVolumes);
    assertEquals(Integer.valueOf(60), masterDeviceInfo.volumeSize);
  }

  @Test
  public void testMigrationWithAzOverrides() {
    // Setup: AZ overrides with storage class
    Map<String, String> azOverrides = new HashMap<>();
    azOverrides.put(
        az1.getCode(),
        "storage:\n"
            + "  tserver:\n"
            + "    storageClass: az1-tserver-sc\n"
            + "  master:\n"
            + "    storageClass: az1-master-sc\n"
            + "gflags:\n"
            + "  tserver:\n"
            + "    az_flag: az_value");

    Universe universe = createTestUniverse("test-universe-3", null, azOverrides, null, null);

    // Run migration
    V437__Populate_DeviceInfo_StorageClass.migrateDeviceInfoStorageClass();

    // Verify: deviceInfo should have storageClass from azOverrides (for base)
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    DeviceInfo deviceInfo =
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az1.getUuid())
            .getPerProcess()
            .get(ServerType.TSERVER)
            .getDeviceInfo();
    DeviceInfo masterDeviceInfo =
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az1.getUuid())
            .getPerProcess()
            .get(ServerType.MASTER)
            .getDeviceInfo();
    ;

    // Assert specific AZ override is populated
    assertNotNull(deviceInfo);
    assertEquals("az1-tserver-sc", deviceInfo.storageClass);
    assertNotNull(masterDeviceInfo);
    assertEquals("az1-master-sc", masterDeviceInfo.storageClass);

    // Assert base userIntent override per-process is empty
    assertNull(details.getPrimaryCluster().userIntent.getUserIntentOverrides().getPerProcess());

    // Assert base userIntent is default deviceInfo
    deviceInfo = details.getPrimaryCluster().userIntent.deviceInfo;
    masterDeviceInfo = details.getPrimaryCluster().userIntent.masterDeviceInfo;
    assertEquals(deviceInfo.storageClass, "");
    assertEquals((long) deviceInfo.numVolumes, 2L);
    assertEquals((long) deviceInfo.volumeSize, 100L);
    assertEquals(masterDeviceInfo.storageClass, "");
    assertEquals((long) masterDeviceInfo.numVolumes, 1L);
    assertEquals((long) masterDeviceInfo.volumeSize, 50L);
  }

  @Test
  public void testMigrationWithProviderAzStorageClass() {
    // Setup: AZ has STORAGE_CLASS in overrides
    az1.getAvailabilityZoneDetails()
        .getCloudInfo()
        .getKubernetes()
        .setKubernetesStorageClass("az1-storage-class");
    az1.save();

    Universe universe = createTestUniverse("test-universe-5", null, null, null, null);

    // Run migration
    V437__Populate_DeviceInfo_StorageClass.migrateDeviceInfoStorageClass();

    // Verify: deviceInfo should have storageClass from AZ
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    DeviceInfo deviceInfo =
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az1.getUuid())
            .getPerProcess()
            .get(ServerType.TSERVER)
            .getDeviceInfo();
    DeviceInfo masterDeviceInfo =
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az1.getUuid())
            .getPerProcess()
            .get(ServerType.MASTER)
            .getDeviceInfo();

    // Assert specific AZ override is populated
    assertNotNull(deviceInfo);
    assertEquals("az1-storage-class", deviceInfo.storageClass);
    assertNotNull(masterDeviceInfo);
    assertEquals("az1-storage-class", masterDeviceInfo.storageClass);

    // Assert other az's deviceInfo is null
    assertNull(
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az2.getUuid()));
    assertNull(
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az3.getUuid()));

    // Assert base userIntent override is empty
    assertNull(details.getPrimaryCluster().userIntent.getUserIntentOverrides().getPerProcess());
    assertNull(details.getPrimaryCluster().userIntent.getUserIntentOverrides().getPerProcess());

    // Assert base userIntent is default deviceInfo
    deviceInfo = details.getPrimaryCluster().userIntent.deviceInfo;
    masterDeviceInfo = details.getPrimaryCluster().userIntent.masterDeviceInfo;
    assertEquals(deviceInfo.storageClass, "");
    assertEquals((long) deviceInfo.numVolumes, 2L);
    assertEquals((long) deviceInfo.volumeSize, 100L);
    assertEquals(masterDeviceInfo.storageClass, "");
    assertEquals((long) masterDeviceInfo.numVolumes, 1L);
    assertEquals((long) masterDeviceInfo.volumeSize, 50L);
  }

  @Test
  public void testMigrationPriorityAzOverridesUniverseOverridesProviderOverrides() {
    // Setup: Multiple sources - priority should be: azOverrides > universeOverrides >
    // provider
    kubernetesProvider
        .getDetails()
        .getCloudInfo()
        .getKubernetes()
        .setKubernetesStorageClass("provider-sc");
    kubernetesProvider.save();

    String universeOverrides =
        "storage:\n"
            + "  tserver:\n"
            + "    storageClass: universe-ts-sc\n"
            + "  master:\n"
            + "    storageClass: universe-ms-sc";

    Map<String, String> azOverrides = new HashMap<>();
    azOverrides.put(
        az1.getCode(),
        "storage:\n"
            + "  tserver:\n"
            + "    storageClass: az-ts-sc\n"
            + "  master:\n"
            + "    storageClass: az-ms-sc");

    Universe universe =
        createTestUniverse("test-universe-6", universeOverrides, azOverrides, null, null);

    // Run migration
    V437__Populate_DeviceInfo_StorageClass.migrateDeviceInfoStorageClass();

    // Verify: azOverrides should win (higher priority than universeOverrides)
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();

    DeviceInfo deviceInfo =
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az1.getUuid())
            .getPerProcess()
            .get(ServerType.TSERVER)
            .getDeviceInfo();
    DeviceInfo masterDeviceInfo =
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az1.getUuid())
            .getPerProcess()
            .get(ServerType.MASTER)
            .getDeviceInfo();
    ;
    // Assert specific AZ override is populated
    assertNotNull(deviceInfo);
    assertEquals("az-ts-sc", deviceInfo.storageClass);
    assertNotNull(masterDeviceInfo);
    assertEquals("az-ms-sc", masterDeviceInfo.storageClass);

    // Assert other az's overridden deviceInfo is null
    assertNull(
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az2.getUuid()));
    assertNull(
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az3.getUuid()));

    // Assert base userIntent is populated with universe override's storage class
    deviceInfo = details.getPrimaryCluster().userIntent.deviceInfo;
    masterDeviceInfo = details.getPrimaryCluster().userIntent.masterDeviceInfo;
    assertNotNull(deviceInfo);
    assertEquals("universe-ts-sc", deviceInfo.storageClass);
    assertNotNull(masterDeviceInfo);
    assertEquals("universe-ms-sc", masterDeviceInfo.storageClass);
  }

  @Test
  public void testMigrationPriorityAzOverridesProviderAzOverrides() {
    // Setup: Multiple sources - priority should be: azOverrides > provider AZ overrides
    az1.getAvailabilityZoneDetails()
        .getCloudInfo()
        .getKubernetes()
        .setKubernetesStorageClass("az1-storage-class");
    az1.save();
    az2.getAvailabilityZoneDetails()
        .getCloudInfo()
        .getKubernetes()
        .setKubernetesStorageClass("az2-storage-class");
    az2.save();

    Map<String, String> azOverrides = new HashMap<>();
    azOverrides.put(
        az1.getCode(),
        "storage:\n"
            + "  tserver:\n"
            + "    storageClass: az1-ts-sc\n"
            + "  master:\n"
            + "    storageClass: az1-ms-sc");
    azOverrides.put(
        az3.getCode(),
        "storage:\n"
            + "  tserver:\n"
            + "    storageClass: az3-ts-sc\n"
            + "  master:\n"
            + "    storageClass: az3-ms-sc");

    Universe universe = createTestUniverse("test-universe-6", null, azOverrides, null, null);

    // Run migration
    V437__Populate_DeviceInfo_StorageClass.migrateDeviceInfoStorageClass();

    // Verify: azOverrides should win (higher priority than universeOverrides)
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();

    // Verify for AZ1: Kubernetes AZ overriddes are used
    DeviceInfo deviceInfo =
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az1.getUuid())
            .getPerProcess()
            .get(ServerType.TSERVER)
            .getDeviceInfo();
    DeviceInfo masterDeviceInfo =
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az1.getUuid())
            .getPerProcess()
            .get(ServerType.MASTER)
            .getDeviceInfo();
    // Assert specific AZ override is populated
    assertNotNull(deviceInfo);
    assertEquals("az1-ts-sc", deviceInfo.storageClass);
    assertNotNull(masterDeviceInfo);
    assertEquals("az1-ms-sc", masterDeviceInfo.storageClass);

    // Verify for AZ3: Kubernetes AZ overriddes are used
    deviceInfo =
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az3.getUuid())
            .getPerProcess()
            .get(ServerType.TSERVER)
            .getDeviceInfo();
    masterDeviceInfo =
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az3.getUuid())
            .getPerProcess()
            .get(ServerType.MASTER)
            .getDeviceInfo();
    // Assert specific AZ override is populated
    assertNotNull(deviceInfo);
    assertEquals("az3-ts-sc", deviceInfo.storageClass);
    assertNotNull(masterDeviceInfo);
    assertEquals("az3-ms-sc", masterDeviceInfo.storageClass);

    // Verify for AZ2: Provider AZ overriddes are used
    deviceInfo =
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az2.getUuid())
            .getPerProcess()
            .get(ServerType.TSERVER)
            .getDeviceInfo();
    masterDeviceInfo =
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az2.getUuid())
            .getPerProcess()
            .get(ServerType.MASTER)
            .getDeviceInfo();
    // Assert specific AZ override is populated
    assertNotNull(deviceInfo);
    assertEquals("az2-storage-class", deviceInfo.storageClass);
    assertNotNull(masterDeviceInfo);
    assertEquals("az2-storage-class", masterDeviceInfo.storageClass);

    // Assert base userIntent is default deviceInfo
    deviceInfo = details.getPrimaryCluster().userIntent.deviceInfo;
    masterDeviceInfo = details.getPrimaryCluster().userIntent.masterDeviceInfo;
    assertEquals(deviceInfo.storageClass, "");
    assertEquals((long) deviceInfo.numVolumes, 2L);
    assertEquals((long) deviceInfo.volumeSize, 100L);
    assertEquals(masterDeviceInfo.storageClass, "");
    assertEquals((long) masterDeviceInfo.numVolumes, 1L);
    assertEquals((long) masterDeviceInfo.volumeSize, 50L);
  }

  @Test
  public void testMigrationPriorityUniverseOverridesUniverseAzOverrides() {
    // Setup: Multiple sources - priority should be: universeOverrides >
    // provider az overrides
    az1.getAvailabilityZoneDetails()
        .getCloudInfo()
        .getKubernetes()
        .setKubernetesStorageClass("az1-storage-class");
    az1.save();

    String universeOverrides =
        "storage:\n"
            + "  tserver:\n"
            + "    storageClass: universe-ts-sc\n"
            + "  master:\n"
            + "    storageClass: universe-ms-sc";

    Map<String, String> azOverrides = new HashMap<>();
    azOverrides.put(
        az1.getCode(),
        "storage:\n"
            + "  tserver:\n"
            + "    storageClass: az1-ts-sc\n"
            + "  master:\n"
            + "    storageClass: az1-ms-sc");
    azOverrides.put(
        az3.getCode(),
        "storage:\n"
            + "  tserver:\n"
            + "    storageClass: az3-ts-sc\n"
            + "  master:\n"
            + "    storageClass: az3-ms-sc");

    Universe universe =
        createTestUniverse("test-universe-6", universeOverrides, azOverrides, null, null);

    // Run migration
    V437__Populate_DeviceInfo_StorageClass.migrateDeviceInfoStorageClass();

    // Verify: universe overrides should win over provider storage class
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();

    // Verify for AZ1: Kubernetes AZ overriddes are used
    DeviceInfo deviceInfo =
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az1.getUuid())
            .getPerProcess()
            .get(ServerType.TSERVER)
            .getDeviceInfo();
    DeviceInfo masterDeviceInfo =
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az1.getUuid())
            .getPerProcess()
            .get(ServerType.MASTER)
            .getDeviceInfo();
    // Assert specific AZ override is populated
    assertNotNull(deviceInfo);
    assertEquals("az1-ts-sc", deviceInfo.storageClass);
    assertNotNull(masterDeviceInfo);
    assertEquals("az1-ms-sc", masterDeviceInfo.storageClass);

    // Verify for AZ3: Kubernetes AZ overriddes are used
    deviceInfo =
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az3.getUuid())
            .getPerProcess()
            .get(ServerType.TSERVER)
            .getDeviceInfo();
    masterDeviceInfo =
        details
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az3.getUuid())
            .getPerProcess()
            .get(ServerType.MASTER)
            .getDeviceInfo();
    // Assert specific AZ override is populated
    assertNotNull(deviceInfo);
    assertEquals("az3-ts-sc", deviceInfo.storageClass);
    assertNotNull(masterDeviceInfo);
    assertEquals("az3-ms-sc", masterDeviceInfo.storageClass);

    // Assert base userIntent is populated with universe override's storage class
    deviceInfo = details.getPrimaryCluster().userIntent.deviceInfo;
    masterDeviceInfo = details.getPrimaryCluster().userIntent.masterDeviceInfo;
    assertNotNull(deviceInfo);
    assertEquals("universe-ts-sc", deviceInfo.storageClass);
    assertNotNull(masterDeviceInfo);
    assertEquals("universe-ms-sc", masterDeviceInfo.storageClass);
  }

  @Test
  public void testMigrationPriorityUniverseOverridesProviderAzOverrides() {
    // Setup: Multiple sources - priority should be: universeOverrides >
    // provider az overrides
    az1.getAvailabilityZoneDetails()
        .getCloudInfo()
        .getKubernetes()
        .setKubernetesStorageClass("az1-storage-class");
    az1.save();

    String universeOverrides =
        "storage:\n"
            + "  tserver:\n"
            + "    storageClass: universe-ts-sc\n"
            + "  master:\n"
            + "    storageClass: universe-ms-sc";

    Universe universe = createTestUniverse("test-universe-6", universeOverrides, null, null, null);

    // Run migration
    V437__Populate_DeviceInfo_StorageClass.migrateDeviceInfoStorageClass();

    // Verify: universe overrides should win over provider storage class
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();

    assertNull(details.getPrimaryCluster().userIntent.getUserIntentOverrides());

    // Assert base userIntent is populated with universe override's storage class
    DeviceInfo deviceInfo = details.getPrimaryCluster().userIntent.deviceInfo;
    DeviceInfo masterDeviceInfo = details.getPrimaryCluster().userIntent.masterDeviceInfo;
    assertNotNull(deviceInfo);
    assertEquals("universe-ts-sc", deviceInfo.storageClass);
    assertNotNull(masterDeviceInfo);
    assertEquals("universe-ms-sc", masterDeviceInfo.storageClass);
  }

  @Test
  public void testMigrationWithExistingStorageClass() {
    // Setup: Universe already has storageClass in deviceInfo
    DeviceInfo existingDeviceInfo = new DeviceInfo();
    existingDeviceInfo.storageClass = "existing-sc";
    existingDeviceInfo.numVolumes = 2;
    existingDeviceInfo.volumeSize = 100;

    String universeOverrides = "storage:\n" + "  tserver:\n" + "    storageClass: universe-sc";

    Universe universe =
        createTestUniverse("test-universe-7", universeOverrides, null, existingDeviceInfo, null);

    // Run migration
    V437__Populate_DeviceInfo_StorageClass.migrateDeviceInfoStorageClass();

    // Verify: Should have universe overrides as that takes precedence
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    DeviceInfo deviceInfo = details.getPrimaryCluster().userIntent.deviceInfo;

    assertEquals("universe-sc", deviceInfo.storageClass);
  }

  @Test
  public void testMigrationIdempotent() {
    // Setup: Universe with storage class
    kubernetesProvider
        .getDetails()
        .getCloudInfo()
        .getKubernetes()
        .setKubernetesStorageClass("provider-sc");
    kubernetesProvider.save();

    Universe universe = createTestUniverse("test-universe-8", null, null, null, null);

    // Run migration first time
    V437__Populate_DeviceInfo_StorageClass.migrateDeviceInfoStorageClass();

    // Mark as migrated
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    universe.updateConfig(ImmutableMap.of("volumeMigrated", "true"));
    universe.save();

    // Run migration second time
    V437__Populate_DeviceInfo_StorageClass.migrateDeviceInfoStorageClass();

    // Verify: Should not process again (idempotent)
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    // The migration should have skipped this universe
    assertTrue(
        "Universe should be marked as migrated",
        universe.getConfig().containsKey("volumeMigrated"));
  }

  @Test
  public void testMigrationWithProviderOverrides() {
    // Setup: Provider has OVERRIDES with storage section
    String providerOverrides =
        "storage:\n"
            + "  tserver:\n"
            + "    storageClass: provider-override-tserver-sc\n"
            + "    count: 4\n"
            + "  master:\n"
            + "    storageClass: provider-override-master-sc\n"
            + "    count: 2";
    kubernetesProvider.setConfig(ImmutableMap.of("OVERRIDES", providerOverrides));
    kubernetesProvider.save();

    Universe universe = createTestUniverse("test-universe-9", null, null, null, null);

    // Run migration
    V437__Populate_DeviceInfo_StorageClass.migrateDeviceInfoStorageClass();

    // Verify: deviceInfo should have storageClass from provider overrides
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    DeviceInfo deviceInfo = details.getPrimaryCluster().userIntent.deviceInfo;
    DeviceInfo masterDeviceInfo = details.getPrimaryCluster().userIntent.masterDeviceInfo;

    assertEquals("provider-override-tserver-sc", deviceInfo.storageClass);
    assertEquals(Integer.valueOf(4), deviceInfo.numVolumes);
    assertEquals("provider-override-master-sc", masterDeviceInfo.storageClass);
    assertEquals(Integer.valueOf(2), masterDeviceInfo.numVolumes);
  }

  @Test
  public void testSnapshotModelsCanReadData() {
    // Setup: Create provider with config
    kubernetesProvider.setConfig(
        ImmutableMap.of("STORAGE_CLASS", "test-sc", "OVERRIDES", "test: value"));
    kubernetesProvider.save();

    region1.getDetails().cloudInfo.getKubernetes().setKubernetesStorageClass("region-sc");
    region1.save();

    az1.getDetails().getCloudInfo().getKubernetes().setKubernetesStorageClass("az-sc");
    az1.save();

    // Verify: V437 snapshot models can read the data correctly
    V437.Provider V437Provider = V437.Provider.getOrBadRequest(kubernetesProvider.getUuid());
    assertNotNull("V437 Provider should be found", V437Provider);
    Map<String, String> providerConfig = V437.CloudInfoInterfaceHelper.fetchEnvVars(V437Provider);
    assertEquals("test-sc", providerConfig.get("STORAGE_CLASS"));
    assertEquals("test: value", providerConfig.get("OVERRIDES"));

    V437.Region V437Region = V437.Region.getOrBadRequest(region1.getUuid());
    assertNotNull("V437 Region should be found", V437Region);
    Map<String, String> regionConfig = V437.CloudInfoInterfaceHelper.fetchEnvVars(V437Region);
    assertEquals("region-sc", regionConfig.get("STORAGE_CLASS"));

    V437.AvailabilityZone V437Az = V437.AvailabilityZone.getOrBadRequest(az1.getUuid());
    assertNotNull("V437 AvailabilityZone should be found", V437Az);
    Map<String, String> azConfig = V437.CloudInfoInterfaceHelper.fetchEnvVars(V437Az);
    assertEquals("az-sc", azConfig.get("STORAGE_CLASS"));

    // Verify: V437 Universe model can read universe data
    Universe universe = createTestUniverse("test-snapshot-universe", null, null, null, null);
    V437.Universe V437Universe = V437.Universe.find.byId(universe.getUniverseUUID());
    assertNotNull("V437 Universe should be found", V437Universe);
    assertNotNull(
        "V437 Universe should have universeDetailsJson", V437Universe.universeDetailsJson);
    assertTrue(
        "V437 Universe details should contain universe name",
        V437Universe.universeDetailsJson.contains("test-snapshot-universe"));
  }

  @Test
  public void testMigrationNonKubernetesUniverse() {
    // Setup: Non-Kubernetes universe should be skipped
    Provider awsProvider =
        Provider.create(customer.getUuid(), CloudType.aws, "AWS Provider", new ProviderDetails());
    Region awsRegion = Region.create(awsProvider, "us-west-1", "US West 1", "ami-123");
    AvailabilityZone awsAz =
        AvailabilityZone.createOrThrow(awsRegion, "us-west-1a", "AZ 1", "subnet-1");

    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.universeName = "aws-universe";
    userIntent.provider = awsProvider.getUuid().toString();
    userIntent.providerType = CloudType.aws;
    userIntent.numNodes = 3;
    userIntent.replicationFactor = 3;
    userIntent.instanceType = "c3.xlarge";
    userIntent.regionList = ImmutableList.of(awsRegion.getUuid());
    userIntent.ybSoftwareVersion = "2.0.0.0";
    userIntent.accessKeyCode = "access-key";

    Universe awsUniverse = ModelFactory.createUniverse("aws-universe", customer.getId());
    Universe.saveDetails(
        awsUniverse.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent, true));

    // Run migration
    V437__Populate_DeviceInfo_StorageClass.migrateDeviceInfoStorageClass();

    // Verify: Non-Kubernetes universe should not be modified
    awsUniverse = Universe.getOrBadRequest(awsUniverse.getUniverseUUID());
    assertFalse(
        "Non-Kubernetes universe should not be marked as migrated",
        awsUniverse.getConfig().containsKey("volumeMigrated"));
  }

  private Universe createTestUniverse(
      String universeName,
      String universeOverrides,
      Map<String, String> azOverrides,
      DeviceInfo existingDeviceInfo,
      DeviceInfo existingMasterDeviceInfo) {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.universeName = universeName;
    userIntent.provider = kubernetesProvider.getUuid().toString();
    userIntent.providerType = CloudType.kubernetes;
    userIntent.numNodes = 3;
    userIntent.replicationFactor = 3;
    userIntent.instanceType = "custom";
    userIntent.regionList = ImmutableList.of(region1.getUuid());
    userIntent.ybSoftwareVersion = "2.0.0.0";
    userIntent.accessKeyCode = "access-key";
    userIntent.universeOverrides = universeOverrides;
    userIntent.azOverrides = azOverrides;

    if (existingDeviceInfo != null) {
      userIntent.deviceInfo = existingDeviceInfo;
    }
    if (existingMasterDeviceInfo != null) {
      userIntent.masterDeviceInfo = existingMasterDeviceInfo;
    }

    // Create PlacementInfo with 1 node in each of the 3 AZs
    Map<UUID, Integer> azToNumNodesMap = new HashMap<>();
    azToNumNodesMap.put(az1.getUuid(), 1);
    azToNumNodesMap.put(az2.getUuid(), 1);
    azToNumNodesMap.put(az3.getUuid(), 1);
    PlacementInfo placementInfo = ModelFactory.constructPlacementInfoObject(azToNumNodesMap);

    Universe universe = ModelFactory.createUniverse(universeName, customer.getId());
    Universe.saveDetails(
        universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent, placementInfo, true));

    return universe;
  }
}
