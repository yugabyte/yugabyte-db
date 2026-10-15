package com.yugabyte.yw.cloud.oci;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.VolumeType;
import com.yugabyte.yw.models.PriceComponent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import java.util.List;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;
import org.springframework.test.util.ReflectionTestUtils;
import play.Environment;
import play.libs.Json;

public class OCIInitializerTest extends FakeDBApplication {

  private Customer customer;
  private Provider provider;
  private Region region;
  private OCIInitializer ociInitializer;
  private ConfigHelper mockConfigHelper;
  private CloudQueryHelper mockCloudQueryHelper;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.newProvider(customer, CloudType.oci, "OCI");
    region = Region.create(provider, "us-ashburn-1", "US Ashburn", "yb-image");
    AvailabilityZone.createOrThrow(region, "ashburn-ad-1", "Ashburn AD-1", "subnet-1");
    provider.save();

    ociInitializer = spy(new OCIInitializer());
    mockConfigHelper = mock(ConfigHelper.class);
    mockCloudQueryHelper = mock(CloudQueryHelper.class);
    ReflectionTestUtils.setField(ociInitializer, "configHelper", mockConfigHelper);
    ReflectionTestUtils.setField(ociInitializer, "cloudQueryHelper", mockCloudQueryHelper);
    ReflectionTestUtils.setField(
        ociInitializer, "environment", app.injector().instanceOf(Environment.class));
  }

  @Test
  public void testInitializeLoadsYamlMetadataWhenApiReturnsEmpty() {
    Map<String, Object> instanceTypeMetadata =
        Map.of(
            "VM.Standard.E4.Flex",
            Map.of(
                "numCores",
                4,
                "memSizeGB",
                64,
                "instanceTypeDetails",
                Map.of(
                    "volumeDetailsList",
                    List.of(Map.of("volumeSizeGB", 500, "volumeType", "SSD")))));
    when(mockConfigHelper.getConfig(ConfigType.OCIInstanceTypeMetadata))
        .thenReturn(instanceTypeMetadata);
    when(mockCloudQueryHelper.getInstanceTypes(anyList(), anyString()))
        .thenReturn(Json.newObject());

    ociInitializer.initialize(customer.getUuid(), provider.getUuid());

    InstanceType instanceType = InstanceType.get(provider.getUuid(), "VM.Standard.E4.Flex");
    assertNotNull(instanceType);
    assertEquals(4, (int) instanceType.getNumCores().doubleValue());
    assertEquals(64, (int) instanceType.getMemSizeGB().doubleValue());
    assertEquals(1, instanceType.getInstanceTypeDetails().volumeDetailsList.size());
    assertEquals(
        500,
        instanceType.getInstanceTypeDetails().volumeDetailsList.get(0).volumeSizeGB.intValue());
  }

  @Test
  public void testFamilyFromShape() {
    assertEquals("E4", OCIPriceUtil.familyFromShape("VM.Standard.E4.Flex"));
    assertEquals("E5", OCIPriceUtil.familyFromShape("BM.Standard.E5.192"));
    assertEquals("X9", OCIPriceUtil.familyFromShape("VM.Standard3.Flex"));
    assertEquals("X9", OCIPriceUtil.familyFromShape("BM.Standard3.64"));
    assertEquals("X7", OCIPriceUtil.familyFromShape("VM.Standard2.1"));
    assertEquals("X7", OCIPriceUtil.familyFromShape("VM.Standard2.24"));
    assertEquals("X7", OCIPriceUtil.familyFromShape("BM.Standard2.52"));
    assertEquals("OptimizedX9", OCIPriceUtil.familyFromShape("VM.Optimized3.Flex"));
    assertEquals("E2Micro", OCIPriceUtil.familyFromShape("VM.Standard.E2.1.Micro"));
    assertEquals("E2", OCIPriceUtil.familyFromShape("VM.Standard.E2.2"));
    assertEquals(null, OCIPriceUtil.familyFromShape("VM.DenseIO.E4.Flex"));
    assertEquals(null, OCIPriceUtil.familyFromShape("VM.GPU.A10.1"));
  }

  @Test
  public void testInitializePreservesYamlNvmeWhenApiOmitsLocalDisks() {
    Map<String, Object> instanceTypeMetadata =
        Map.of(
            "VM.DenseIO2.16",
            Map.of(
                "numCores",
                16,
                "memSizeGB",
                240,
                "instanceTypeDetails",
                Map.of(
                    "volumeDetailsList",
                    List.of(
                        Map.of("volumeSizeGB", 6400, "volumeType", "NVME"),
                        Map.of("volumeSizeGB", 6400, "volumeType", "NVME")))));
    when(mockConfigHelper.getConfig(ConfigType.OCIInstanceTypeMetadata))
        .thenReturn(instanceTypeMetadata);
    ObjectNode apiTypes = Json.newObject();
    apiTypes.set("VM.DenseIO2.16", Json.newObject().put("numCores", 16).put("memSizeGb", 240.0));
    when(mockCloudQueryHelper.getInstanceTypes(anyList(), anyString())).thenReturn(apiTypes);

    ociInitializer.initialize(customer.getUuid(), provider.getUuid());

    InstanceType instanceType = InstanceType.get(provider.getUuid(), "VM.DenseIO2.16");
    assertNotNull(instanceType);
    assertEquals(2, instanceType.getInstanceTypeDetails().volumeDetailsList.size());
    assertEquals(
        VolumeType.NVME, instanceType.getInstanceTypeDetails().volumeDetailsList.get(0).volumeType);
    assertEquals(
        6400,
        instanceType.getInstanceTypeDetails().volumeDetailsList.get(0).volumeSizeGB.intValue());
  }

  @Test
  public void testInitializeUsesApiLocalDisksForFixedDenseIO() {
    when(mockConfigHelper.getConfig(ConfigType.OCIInstanceTypeMetadata)).thenReturn(Map.of());
    ObjectNode apiTypes = Json.newObject();
    apiTypes.set(
        "BM.DenseIO.E4.128",
        Json.newObject()
            .put("numCores", 128)
            .put("memSizeGb", 2048.0)
            .put("localDisks", 8)
            .put("localDisksInGbs", 54400));
    when(mockCloudQueryHelper.getInstanceTypes(anyList(), anyString())).thenReturn(apiTypes);

    ociInitializer.initialize(customer.getUuid(), provider.getUuid());

    InstanceType instanceType = InstanceType.get(provider.getUuid(), "BM.DenseIO.E4.128");
    assertNotNull(instanceType);
    assertEquals(8, instanceType.getInstanceTypeDetails().volumeDetailsList.size());
    assertEquals(
        VolumeType.NVME, instanceType.getInstanceTypeDetails().volumeDetailsList.get(0).volumeType);
    assertEquals(
        6800,
        instanceType.getInstanceTypeDetails().volumeDetailsList.get(0).volumeSizeGB.intValue());
  }

  @Test
  public void testInitializeKeepsYamlFlexTripleWhenApiReportsMaxDisks() {
    Map<String, Object> instanceTypeMetadata =
        Map.of(
            "VM.DenseIO.E5.Flex",
            Map.of(
                "numCores",
                8,
                "memSizeGB",
                96,
                "instanceTypeDetails",
                Map.of(
                    "volumeDetailsList",
                    List.of(Map.of("volumeSizeGB", 6800, "volumeType", "NVME")))));
    when(mockConfigHelper.getConfig(ConfigType.OCIInstanceTypeMetadata))
        .thenReturn(instanceTypeMetadata);
    ObjectNode apiTypes = Json.newObject();
    apiTypes.set(
        "VM.DenseIO.E5.Flex",
        Json.newObject()
            .put("numCores", 8)
            .put("memSizeGb", 96.0)
            .put("localDisks", 6)
            .put("localDisksInGbs", 40800));
    when(mockCloudQueryHelper.getInstanceTypes(anyList(), anyString())).thenReturn(apiTypes);

    ociInitializer.initialize(customer.getUuid(), provider.getUuid());

    InstanceType instanceType = InstanceType.get(provider.getUuid(), "VM.DenseIO.E5.Flex");
    assertNotNull(instanceType);
    assertEquals(8.0, instanceType.getNumCores(), 0.0);
    assertEquals(96.0, instanceType.getMemSizeGB(), 0.0);
    assertEquals(1, instanceType.getInstanceTypeDetails().volumeDetailsList.size());
    assertEquals(
        VolumeType.NVME, instanceType.getInstanceTypeDetails().volumeDetailsList.get(0).volumeType);
    assertEquals(
        6800,
        instanceType.getInstanceTypeDetails().volumeDetailsList.get(0).volumeSizeGB.intValue());
  }

  @Test
  public void testInitializePrefersYamlFlexResourcesOverApi() {
    Map<String, Object> instanceTypeMetadata =
        Map.of(
            "VM.DenseIO.E4.Flex",
            Map.of(
                "numCores",
                8,
                "memSizeGB",
                128,
                "instanceTypeDetails",
                Map.of(
                    "volumeDetailsList",
                    List.of(Map.of("volumeSizeGB", 6800, "volumeType", "NVME")))));
    when(mockConfigHelper.getConfig(ConfigType.OCIInstanceTypeMetadata))
        .thenReturn(instanceTypeMetadata);
    ObjectNode apiTypes = Json.newObject();
    apiTypes.set(
        "VM.DenseIO.E4.Flex",
        Json.newObject()
            .put("numCores", 2)
            .put("memSizeGb", 16.0)
            .put("localDisks", 6)
            .put("localDisksInGbs", 40800));
    when(mockCloudQueryHelper.getInstanceTypes(anyList(), anyString())).thenReturn(apiTypes);

    ociInitializer.initialize(customer.getUuid(), provider.getUuid());

    InstanceType instanceType = InstanceType.get(provider.getUuid(), "VM.DenseIO.E4.Flex");
    assertNotNull(instanceType);
    assertEquals(8.0, instanceType.getNumCores(), 0.0);
    assertEquals(128.0, instanceType.getMemSizeGB(), 0.0);
    assertEquals(1, instanceType.getInstanceTypeDetails().volumeDetailsList.size());
  }

  @Test
  public void testInitializeSkipsApiFlexNvmeDefaults() {
    when(mockConfigHelper.getConfig(ConfigType.OCIInstanceTypeMetadata)).thenReturn(Map.of());
    ObjectNode apiTypes = Json.newObject();
    apiTypes.set(
        "VM.DenseIO.E5.Flex",
        Json.newObject()
            .put("numCores", 1)
            .put("memSizeGb", 16.0)
            .put("localDisks", 6)
            .put("localDisksInGbs", 40800));
    when(mockCloudQueryHelper.getInstanceTypes(anyList(), anyString())).thenReturn(apiTypes);

    ociInitializer.initialize(customer.getUuid(), provider.getUuid());

    assertEquals(null, InstanceType.get(provider.getUuid(), "VM.DenseIO.E5.Flex"));
  }

  @Test
  public void testInitializeKeepsSsdDefaultForGpuLocalDisks() {
    when(mockConfigHelper.getConfig(ConfigType.OCIInstanceTypeMetadata)).thenReturn(Map.of());
    ObjectNode apiTypes = Json.newObject();
    apiTypes.set(
        "BM.GPU4.8",
        Json.newObject()
            .put("numCores", 52)
            .put("memSizeGb", 768.0)
            .put("localDisks", 4)
            .put("localDisksInGbs", 27200));
    when(mockCloudQueryHelper.getInstanceTypes(anyList(), anyString())).thenReturn(apiTypes);

    ociInitializer.initialize(customer.getUuid(), provider.getUuid());

    InstanceType instanceType = InstanceType.get(provider.getUuid(), "BM.GPU4.8");
    assertNotNull(instanceType);
    assertEquals(
        VolumeType.SSD, instanceType.getInstanceTypeDetails().volumeDetailsList.get(0).volumeType);
  }

  @Test
  public void testHasEphemeralStorageForOciDenseIO() {
    assertEquals(
        true,
        UniverseDefinitionTaskParams.hasEphemeralStorage(
            CloudType.oci, "VM.DenseIO.E4.Flex", null));
    assertEquals(
        true, UniverseDefinitionTaskParams.hasEphemeralStorage(CloudType.oci, "BM.HPC2.36", null));
    assertEquals(
        false,
        UniverseDefinitionTaskParams.hasEphemeralStorage(
            CloudType.oci, "VM.Standard.E4.Flex", null));
  }

  @Test
  public void testInitializeStoresBundledPriceMeters() {
    when(mockConfigHelper.getConfig(ConfigType.OCIInstanceTypeMetadata)).thenReturn(Map.of());
    when(mockCloudQueryHelper.getInstanceTypes(any(), anyString())).thenReturn(Json.newObject());

    ociInitializer.initialize(customer.getUuid(), provider.getUuid());

    PriceComponent e4Ocpu =
        PriceComponent.get(
            provider.getUuid(), region.getCode(), OCIPriceUtil.ocpuComponentCode("E4"));
    assertNotNull(e4Ocpu);
    assertEquals(0.025, e4Ocpu.getPriceDetails().pricePerHour, 0.0001);

    PriceComponent a1Ocpu =
        PriceComponent.get(
            provider.getUuid(), region.getCode(), OCIPriceUtil.ocpuComponentCode("A1"));
    assertNotNull(a1Ocpu);
    assertEquals(0.01, a1Ocpu.getPriceDetails().pricePerHour, 0.0001);

    PriceComponent e4Memory =
        PriceComponent.get(
            provider.getUuid(), region.getCode(), OCIPriceUtil.memoryComponentCode("E4"));
    assertNotNull(e4Memory);
    assertEquals(0.0015, e4Memory.getPriceDetails().pricePerHour, 0.0001);

    PriceComponent blockStorage =
        PriceComponent.get(
            provider.getUuid(), region.getCode(), OCIPriceUtil.blockStorageComponentCode());
    assertNotNull(blockStorage);
    assertEquals(
        OCIPriceUtil.monthlyToHourly(0.0255), blockStorage.getPriceDetails().pricePerHour, 1e-9);

    PriceComponent blockVpu =
        PriceComponent.get(
            provider.getUuid(), region.getCode(), OCIPriceUtil.blockVpuComponentCode());
    assertNotNull(blockVpu);
    assertEquals(
        OCIPriceUtil.monthlyToHourly(0.0017), blockVpu.getPriceDetails().pricePerHour, 1e-9);
  }
}
