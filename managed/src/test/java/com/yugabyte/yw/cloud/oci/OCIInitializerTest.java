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
import play.libs.Json;

public class OCIInitializerTest extends FakeDBApplication {

  private Customer customer;
  private Provider provider;
  private OCIInitializer ociInitializer;
  private ConfigHelper mockConfigHelper;
  private CloudQueryHelper mockCloudQueryHelper;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.newProvider(customer, CloudType.oci, "OCI");
    Region region = Region.create(provider, "us-ashburn-1", "US Ashburn", "yb-image");
    AvailabilityZone.createOrThrow(region, "ashburn-ad-1", "Ashburn AD-1", "subnet-1");
    provider.save();

    ociInitializer = spy(new OCIInitializer());
    mockConfigHelper = mock(ConfigHelper.class);
    mockCloudQueryHelper = mock(CloudQueryHelper.class);
    ReflectionTestUtils.setField(ociInitializer, "configHelper", mockConfigHelper);
    ReflectionTestUtils.setField(ociInitializer, "cloudQueryHelper", mockCloudQueryHelper);
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
  public void testInitializeAddsApiInstanceTypesAndPriceComponents() {
    when(mockConfigHelper.getConfig(ConfigType.OCIInstanceTypeMetadata)).thenReturn(Map.of());

    ObjectNode apiResult = Json.newObject();
    ObjectNode instanceType = Json.newObject();
    instanceType.put("numCores", 2);
    instanceType.put("memSizeGb", 12.5);
    instanceType.putObject("prices").put("us-ashburn-1", 0.25);
    apiResult.set("VM.Standard.A1.Flex", instanceType);
    when(mockCloudQueryHelper.getInstanceTypes(any(), anyString())).thenReturn(apiResult);

    ociInitializer.initialize(customer.getUuid(), provider.getUuid());

    InstanceType createdType = InstanceType.get(provider.getUuid(), "VM.Standard.A1.Flex");
    assertNotNull(createdType);
    assertEquals(2, (int) createdType.getNumCores().doubleValue());
    assertEquals(12.5, createdType.getMemSizeGB().doubleValue(), 0.001);

    PriceComponent component =
        PriceComponent.get(provider.getUuid(), "us-ashburn-1", "VM.Standard.A1.Flex");
    assertNotNull(component);
    assertEquals(0.25, component.getPriceDetails().pricePerHour, 0.0001);
    assertEquals(6.0, component.getPriceDetails().pricePerDay, 0.0001);
    assertEquals(180.0, component.getPriceDetails().pricePerMonth, 0.0001);
  }
}
