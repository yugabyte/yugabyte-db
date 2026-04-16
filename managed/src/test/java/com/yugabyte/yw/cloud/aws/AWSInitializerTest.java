package com.yugabyte.yw.cloud.aws;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.ProviderDetails.CloudInfo;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import java.util.Arrays;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;

@Slf4j
public class AWSInitializerTest extends FakeDBApplication {

  private Customer customer;
  private Provider defaultProvider;
  private Region defaultRegion;
  AWSInitializer awsInitializer;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.awsProvider(customer);
    defaultRegion = new Region();
    defaultRegion.setProvider(defaultProvider);
    defaultRegion.setCode("us-east-2");
    defaultRegion.setName("us-east-2");
    AvailabilityZone az = new AvailabilityZone();
    az.setCode("us-east-2a");
    az.setName("us-east-2a");
    defaultRegion.setZones(Arrays.asList(az));
    defaultProvider.getRegions().add(defaultRegion);
    ProviderDetails providerDetails = new ProviderDetails();
    CloudInfo cloudInfo = new CloudInfo();
    cloudInfo.aws = new AWSCloudInfo();
    cloudInfo.aws.setAwsAccessKeyID("accessKey");
    cloudInfo.aws.setAwsAccessKeySecret("accessKeySecret");
    providerDetails.setCloudInfo(cloudInfo);
    defaultProvider.setDetails(providerDetails);
    defaultProvider.save();
  }

  @Test
  public void testAWSInitializerAllInstanceTypes() {

    assertTrue(
        UniverseDefinitionTaskParams.hasEphemeralStorage(CloudType.aws, "x2gd.12xlarge", null));
    assertTrue(
        UniverseDefinitionTaskParams.hasEphemeralStorage(CloudType.aws, "i3en.2xlarge", null));

    RuntimeConfigEntry.upsertGlobal("yb.internal.allow_unsupported_instances", "true");
    awsInitializer = app.injector().instanceOf(AWSInitializer.class);
    awsInitializer.initialize(customer.getUuid(), defaultProvider.getUuid());

    InstanceType c6a4x = InstanceType.get(defaultProvider.getUuid(), "c6a.4xlarge");
    assertEquals(16, (int) c6a4x.getNumCores().doubleValue());
    assertEquals(32, (int) c6a4x.getMemSizeGB().doubleValue());

    InstanceType c6id4x = InstanceType.get(defaultProvider.getUuid(), "c6id.4xlarge");
    assertEquals(16, (int) c6id4x.getNumCores().doubleValue());
    assertEquals(32, (int) c6id4x.getMemSizeGB().doubleValue());
    assertEquals(1, c6id4x.getInstanceTypeDetails().volumeDetailsList.size());
    var volDetailsList = c6id4x.getInstanceTypeDetails().volumeDetailsList.get(0);
    assertEquals(volDetailsList.volumeType, InstanceType.VolumeType.NVME);
    assertEquals(950, volDetailsList.volumeSizeGB.intValue());

    InstanceType g612x = InstanceType.get(defaultProvider.getUuid(), "g6.12xlarge");
    assertEquals(48, (int) g612x.getNumCores().doubleValue());
    assertEquals(192, (int) g612x.getMemSizeGB().doubleValue());
    assertEquals(4, g612x.getInstanceTypeDetails().volumeDetailsList.size());
    volDetailsList = g612x.getInstanceTypeDetails().volumeDetailsList.get(0);
    assertEquals(volDetailsList.volumeType, InstanceType.VolumeType.NVME);
    assertEquals(940, volDetailsList.volumeSizeGB.intValue());
  }

  @Test
  public void testAWSInitializerAllowedInstanceTypes() {
    RuntimeConfigEntry.upsertGlobal("yb.internal.allow_unsupported_instances", "false");
    awsInitializer = app.injector().instanceOf(AWSInitializer.class);
    awsInitializer.initialize(customer.getUuid(), defaultProvider.getUuid());

    InstanceType c6a4x = InstanceType.get(defaultProvider.getUuid(), "c6a.4xlarge");
    assertEquals(16, (int) c6a4x.getNumCores().doubleValue());
    assertEquals(32, (int) c6a4x.getMemSizeGB().doubleValue());

    InstanceType c6id4x = null;
    try {
      c6id4x = InstanceType.get(defaultProvider.getUuid(), "c6id.4xlarge");
    } catch (Exception ex) {
      // ignore
    }
    assertEquals(null, c6id4x);
  }
}
