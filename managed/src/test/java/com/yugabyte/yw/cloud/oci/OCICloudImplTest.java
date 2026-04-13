package com.yugabyte.yw.cloud.oci;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.mockito.Mockito.spy;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.CloudUtil.Protocol;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.ProviderDetails.CloudInfo;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.NLBHealthCheckConfiguration;
import com.yugabyte.yw.models.helpers.provider.OCICloudInfo;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import org.junit.Before;
import org.junit.Test;

public class OCICloudImplTest extends FakeDBApplication {

  private OCICloudImpl ociCloudImpl;
  private Provider defaultProvider;

  @Before
  public void setup() {
    ociCloudImpl = spy(new OCICloudImpl());

    Customer customer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.newProvider(customer, CloudType.oci, "OCI");

    ProviderDetails providerDetails = new ProviderDetails();
    CloudInfo cloudInfo = new CloudInfo();
    cloudInfo.oci =
        OCICloudInfo.builder()
            .ociTenancyId("ocid1.tenancy.oc1..example")
            .ociUserId("ocid1.user.oc1..example")
            .ociFingerprint("11:22:33:44:55")
            .ociPrivateKeyContent("-----BEGIN PRIVATE KEY-----\nkey\n-----END PRIVATE KEY-----")
            .ociCompartmentId("ocid1.compartment.oc1..example")
            .ociRegion("invalid-region")
            .build();
    providerDetails.setCloudInfo(cloudInfo);
    defaultProvider.setDetails(providerDetails);
  }

  @Test
  public void testOfferedZonesByInstanceTypeReturnsUnionOfZones() {
    Region regionOne = new Region();
    regionOne.setCode("us-ashburn-1");
    Region regionTwo = new Region();
    regionTwo.setCode("us-phoenix-1");

    Map<Region, Set<String>> azByRegion = new HashMap<>();
    azByRegion.put(regionOne, Set.of("ad-1", "ad-2"));
    azByRegion.put(regionTwo, Set.of("ad-3"));

    Set<String> instanceTypes = Set.of("VM.Standard.E4.Flex", "VM.Standard.A1.Flex");

    Map<String, Set<String>> result =
        ociCloudImpl.offeredZonesByInstanceType(defaultProvider, azByRegion, instanceTypes);

    assertEquals(2, result.size());
    assertEquals(
        new HashSet<>(Arrays.asList("ad-1", "ad-2", "ad-3")), result.get("VM.Standard.E4.Flex"));
    assertEquals(
        new HashSet<>(Arrays.asList("ad-1", "ad-2", "ad-3")), result.get("VM.Standard.A1.Flex"));
  }

  @Test
  public void testIsValidCredsReturnsFalseWhenProviderCloudInfoMissing() {
    defaultProvider.setDetails(new ProviderDetails());
    assertFalse(ociCloudImpl.isValidCreds(defaultProvider));
  }

  @Test
  public void testIsValidCredsReturnsFalseWhenRequiredEnvVarMissing() {
    defaultProvider.getDetails().getCloudInfo().getOci().setOciCompartmentId("");
    assertFalse(ociCloudImpl.isValidCreds(defaultProvider));
  }

  @Test
  public void testIsValidCredsReturnsFalseForInvalidRegion() {
    assertFalse(ociCloudImpl.isValidCreds(defaultProvider));
  }

  @Test
  public void testManageNodeGroupThrowsUnsupportedOperationException() {
    Region region = new Region();
    region.setProvider(defaultProvider);
    region.setCode("us-ashburn-1");
    AvailabilityZone az = new AvailabilityZone();
    az.setCode("ashburn-ad-1");
    region.setZones(Arrays.asList(az));
    defaultProvider.getRegions().add(region);

    UnsupportedOperationException exception =
        assertThrows(
            UnsupportedOperationException.class,
            () ->
                ociCloudImpl.manageNodeGroup(
                    defaultProvider,
                    region.getCode(),
                    "test-lb",
                    new HashMap<>(),
                    Arrays.asList(5433),
                    new NLBHealthCheckConfiguration(
                        Arrays.asList(5433), Protocol.TCP, Arrays.asList())));
    assertEquals("OCI load balancer management is not yet supported", exception.getMessage());
  }
}
