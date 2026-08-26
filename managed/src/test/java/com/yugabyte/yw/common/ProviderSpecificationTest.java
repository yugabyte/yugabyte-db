// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.forms.HierarchicalNodesSpec;
import com.yugabyte.yw.forms.HierarchicalNodesSpec.AzNodesSpec;
import com.yugabyte.yw.forms.HierarchicalNodesSpec.RegionNodesSpec;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.ProxyConfig;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class ProviderSpecificationTest extends FakeDBApplication {

  private Customer customer;
  private Provider provider;
  private UUID imageBundleUUID = UUID.randomUUID();
  private Map<String, String> tags = Map.of("a", "b");
  private DeviceInfo deviceInfo;
  private ProxyConfig proxyConfig;

  @Before
  public void init() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.newProvider(customer, Common.CloudType.gcp);

    deviceInfo = new DeviceInfo();
    deviceInfo.storageType = PublicCloudConstants.StorageType.GP2;
    deviceInfo.diskIops = 1000;
    deviceInfo.numVolumes = 3;
    deviceInfo.throughput = 5;
    deviceInfo.volumeSize = 1500;

    proxyConfig = new ProxyConfig();
    proxyConfig.setHttpsProxy("https proxy");
  }

  @Test
  public void testSingleProviderSpec() {
    Region region = Region.create(provider, "default-region", "Default Region", "yb-image");
    AvailabilityZone az =
        AvailabilityZone.createOrThrow(region, "default-az", "Default AZ", "subnet");

    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    UniverseDefinitionTaskParams.ProviderSpecification providerSpecification =
        new UniverseDefinitionTaskParams.ProviderSpecification();
    providerSpecification.setProviderUUID(provider.getUuid());
    providerSpecification.setProviderType(Common.CloudType.gcp);
    providerSpecification.setInstanceTags(tags);
    providerSpecification.setImageBundleUUID(imageBundleUUID);
    providerSpecification.setEnableLoadBalancer(true);
    providerSpecification.setHelmOverrides("Helm overrides");
    providerSpecification.setAccessKeyCode("accessKey");
    providerSpecification.setEnableLoadBalancer(true);
    providerSpecification.setExposingServiceState(
        UniverseDefinitionTaskParams.ExposingServiceState.UNEXPOSED);
    providerSpecification.setAwsInstanceProfile("aws profile");

    providerSpecification.setNodesSpecs(
        HierarchicalNodesSpec.RootNodesSpec.builder()
            .tserverSpecification(
                HierarchicalNodesSpec.NodeSpec.builder()
                    .backupProxyConfig(proxyConfig)
                    .instanceType("tserverType")
                    .cgroupSize(10)
                    .deviceInfo(deviceInfo)
                    .build())
            .build());

    userIntent.providerSpecifications = Collections.singletonList(providerSpecification);

    NodeDetails nodeDetails = new NodeDetails();
    nodeDetails.azUuid = az.getUuid();

    assertEquals(Set.of(provider.getUuid()), userIntent.getAllProviderUUIDs());
    assertEquals(Arrays.asList(Common.CloudType.gcp), userIntent.getAllCloudTypes());
    assertEquals(providerSpecification, userIntent.getProviderSpecification(provider.getUuid()));
    assertEquals(List.of(imageBundleUUID), userIntent.getAllImageBundles());
    assertEquals(imageBundleUUID, userIntent.getImageBundleUUIDForProvider(provider.getUuid()));
    assertEquals("accessKey", userIntent.getAccessKeyCodeForProvider(provider.getUuid()));
    assertEquals(10, userIntent.getCGroupSize(nodeDetails).intValue());
    assertEquals("tserverType", userIntent.getInstanceTypeForNode(nodeDetails));
    assertEquals(deviceInfo, userIntent.getDeviceInfoForNode(nodeDetails));
    assertEquals(proxyConfig, userIntent.getProxyConfig(nodeDetails.azUuid));
  }

  @Test
  public void testRegionLevelInstanceTypeOverride() {
    Region r1 = Region.create(provider, "r1", "Region 1", "yb-image");
    Region r2 = Region.create(provider, "r2", "Region 2", "yb-image");
    AvailabilityZone r1Az =
        AvailabilityZone.createOrThrow(r1, "r1z1", "Region 1 AZ 1", "subnet-r1z1");
    AvailabilityZone r2Az =
        AvailabilityZone.createOrThrow(r2, "r2z1", "Region 2 AZ 1", "subnet-r2z1");

    UniverseDefinitionTaskParams.ProviderSpecification providerSpecification =
        new UniverseDefinitionTaskParams.ProviderSpecification();
    providerSpecification.setProviderUUID(provider.getUuid());
    providerSpecification.setNodesSpecs(
        HierarchicalNodesSpec.RootNodesSpec.builder()
            .tserverSpecification(
                HierarchicalNodesSpec.NodeSpec.builder().instanceType("default-type").build())
            .build()
            .addRegion(
                RegionNodesSpec.builder()
                    .regionCode("r1")
                    .tserverSpecification(
                        HierarchicalNodesSpec.NodeSpec.builder()
                            .instanceType("region-r1-type")
                            .build())
                    .build()));

    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.providerSpecifications = Collections.singletonList(providerSpecification);

    NodeDetails r1Node = new NodeDetails();
    r1Node.azUuid = r1Az.getUuid();
    NodeDetails r2Node = new NodeDetails();
    r2Node.azUuid = r2Az.getUuid();

    assertEquals("region-r1-type", userIntent.getInstanceTypeForNode(r1Node));
    assertEquals("default-type", userIntent.getInstanceTypeForNode(r2Node));
  }

  @Test
  public void testAzLevelInstanceTypeOverride() {
    Region r1 = Region.create(provider, "r1", "Region 1", "yb-image");
    AvailabilityZone r1z1 =
        AvailabilityZone.createOrThrow(r1, "r1z1", "Region 1 AZ 1", "subnet-r1z1");

    Region r2 = Region.create(provider, "r2", "Region 2", "yb-image");
    AvailabilityZone r2z1 =
        AvailabilityZone.createOrThrow(r2, "r2z1", "Region 2 AZ 1", "subnet-r2z1");
    AvailabilityZone r2z2 =
        AvailabilityZone.createOrThrow(r2, "r2z2", "Region 2 AZ 2", "subnet-r2z2");

    UniverseDefinitionTaskParams.ProviderSpecification providerSpecification =
        new UniverseDefinitionTaskParams.ProviderSpecification();
    providerSpecification.setProviderUUID(provider.getUuid());
    providerSpecification.setNodesSpecs(
        HierarchicalNodesSpec.RootNodesSpec.builder()
            .tserverSpecification(
                HierarchicalNodesSpec.NodeSpec.builder().instanceType("default-type").build())
            .build()
            .addRegion(
                RegionNodesSpec.builder()
                    .regionCode("r2")
                    .tserverSpecification(
                        HierarchicalNodesSpec.NodeSpec.builder().instanceType("az-r2-type").build())
                    .build()
                    .addZone(
                        AzNodesSpec.builder()
                            .azCode("r2z2")
                            .tserverSpecification(
                                HierarchicalNodesSpec.NodeSpec.builder()
                                    .instanceType("az-r2z2-type")
                                    .build())
                            .build())));

    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.providerSpecifications = Collections.singletonList(providerSpecification);

    NodeDetails r1Node = new NodeDetails();
    r1Node.azUuid = r1z1.getUuid();

    NodeDetails r2z1Node = new NodeDetails();
    r2z1Node.azUuid = r2z1.getUuid();
    NodeDetails r2z2Node = new NodeDetails();
    r2z2Node.azUuid = r2z2.getUuid();

    assertEquals("default-type", userIntent.getInstanceTypeForNode(r1Node));
    assertEquals("az-r2-type", userIntent.getInstanceTypeForNode(r2z1Node));
    assertEquals("az-r2z2-type", userIntent.getInstanceTypeForNode(r2z2Node));
  }

  @Test
  public void testValidateProviderSpecification() {
    UniverseDefinitionTaskParams.ProviderSpecification providerSpecification =
        new UniverseDefinitionTaskParams.ProviderSpecification();
    providerSpecification.setProviderUUID(provider.getUuid());
    providerSpecification.setProviderType(Common.CloudType.azu);
    providerSpecification.setNodesSpecs(
        HierarchicalNodesSpec.RootNodesSpec.builder()
            .tserverSpecification(
                HierarchicalNodesSpec.NodeSpec.builder().instanceType("tserverType").build())
            .build());
    providerSpecification.validate(false);

    providerSpecification.setProviderUUID(null);
    expectValidationError(providerSpecification, false, "providerUUID must be set");
    providerSpecification.setProviderUUID(provider.getUuid());

    providerSpecification.setProviderType(null);
    expectValidationError(providerSpecification, false, "providerType must be set");
    providerSpecification.setProviderType(Common.CloudType.azu);

    providerSpecification.setNodesSpecs(null);
    expectValidationError(providerSpecification, false, "nodesSpecs must be set");
    providerSpecification.setNodesSpecs(HierarchicalNodesSpec.RootNodesSpec.builder().build());
    expectValidationError(
        providerSpecification, false, "nodesSpecs.tserverSpecification must be set");
  }

  @Test
  public void testValidateProviderSpecificationPartialUpdate() {
    UniverseDefinitionTaskParams.ProviderSpecification providerSpecification =
        new UniverseDefinitionTaskParams.ProviderSpecification();
    providerSpecification.setProviderUUID(provider.getUuid());
    providerSpecification.setProviderType(Common.CloudType.gcp);
    providerSpecification.setNodesSpecs(
        HierarchicalNodesSpec.RootNodesSpec.builder()
            .regionNodesSpecs(
                Arrays.asList(
                    RegionNodesSpec.builder()
                        .regionCode("r1")
                        .azNodesSpecs(
                            Arrays.asList(
                                AzNodesSpec.builder()
                                    .azCode("r1z1")
                                    .tserverSpecification(
                                        HierarchicalNodesSpec.NodeSpec.builder()
                                            .backupProxyConfig(proxyConfig)
                                            .build())
                                    .build()))
                        .build()))
            .build());

    // Partial updates (e.g. proxy config) may omit root tserverSpecification.
    providerSpecification.validate(true);

    providerSpecification.setProviderUUID(null);
    expectValidationError(providerSpecification, true, "providerUUID must be set");
    providerSpecification.setProviderUUID(provider.getUuid());

    providerSpecification.setProviderType(null);
    expectValidationError(providerSpecification, true, "providerType must be set");
    providerSpecification.setProviderType(Common.CloudType.gcp);

    providerSpecification.setNodesSpecs(null);
    expectValidationError(providerSpecification, true, "nodesSpecs must be set");
  }

  @Test
  public void testValidateDuplicateRegionCode() {
    UniverseDefinitionTaskParams.ProviderSpecification providerSpecification =
        new UniverseDefinitionTaskParams.ProviderSpecification();
    providerSpecification.setProviderUUID(provider.getUuid());
    providerSpecification.setProviderType(Common.CloudType.gcp);
    providerSpecification.setNodesSpecs(
        HierarchicalNodesSpec.RootNodesSpec.builder()
            .tserverSpecification(
                HierarchicalNodesSpec.NodeSpec.builder().instanceType("tserverType").build())
            .regionNodesSpecs(
                Arrays.asList(
                    RegionNodesSpec.builder().regionCode("r1").build(),
                    RegionNodesSpec.builder().regionCode("r1").build()))
            .build());
    expectValidationError(providerSpecification, false, "RootNodesSpec: found duplicate code: r1");
  }

  @Test
  public void testValidateDuplicateAzCode() {
    UniverseDefinitionTaskParams.ProviderSpecification providerSpecification =
        new UniverseDefinitionTaskParams.ProviderSpecification();
    providerSpecification.setProviderUUID(provider.getUuid());
    providerSpecification.setProviderType(Common.CloudType.gcp);
    providerSpecification.setNodesSpecs(
        HierarchicalNodesSpec.RootNodesSpec.builder()
            .tserverSpecification(
                HierarchicalNodesSpec.NodeSpec.builder().instanceType("tserverType").build())
            .regionNodesSpecs(
                Arrays.asList(
                    RegionNodesSpec.builder()
                        .regionCode("r1")
                        .azNodesSpecs(
                            Arrays.asList(
                                AzNodesSpec.builder().azCode("z1").build(),
                                AzNodesSpec.builder().azCode("z1").build()))
                        .build()))
            .build());
    expectValidationError(
        providerSpecification, false, "RegionNodesSpec(r1): found duplicate code: z1");
  }

  private void expectValidationError(
      UniverseDefinitionTaskParams.ProviderSpecification providerSpecification,
      boolean isPartialUpdate,
      String expectedMessage) {
    try {
      providerSpecification.validate(isPartialUpdate);
      fail("Expected PlatformServiceException");
    } catch (PlatformServiceException e) {
      assertEquals(BAD_REQUEST, e.getHttpStatus());
      assertEquals(expectedMessage, e.getMessage());
    }
  }
}
