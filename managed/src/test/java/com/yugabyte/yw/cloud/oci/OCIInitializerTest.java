package com.yugabyte.yw.cloud.oci;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
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
