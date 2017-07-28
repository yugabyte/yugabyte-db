// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.cloud;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.PriceComponent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.junit.Before;
import org.junit.Test;

import java.util.HashSet;
import java.util.Iterator;
import java.util.Set;

import static com.yugabyte.yw.cloud.PublicCloudConstants.GP2_SIZE;
import static com.yugabyte.yw.cloud.PublicCloudConstants.IO1_PIOPS;
import static com.yugabyte.yw.cloud.PublicCloudConstants.IO1_SIZE;
import static com.yugabyte.yw.common.ApiUtils.getDummyDeviceInfo;
import static com.yugabyte.yw.common.ApiUtils.getDummyUserIntent;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

public class UniverseResourceDetailsTest extends FakeDBApplication {

  private Provider provider;
  private Region region;
  private AvailabilityZone az;
  private String testInstanceType = "c3.xlarge";
  private double instancePrice = 0.1;
  private double piopsPrice = 0.01;
  private double sizePrice = 0.01;
  private int numVolumes = 2;
  private int volumeSize = 200;
  private int diskIops = 500;

  private Set<NodeDetails> setUpNodeDetailsSet(Iterator<NodeDetails> mockIterator) {
    NodeDetails nodeDetails = new NodeDetails();
    nodeDetails.cloudInfo = new CloudSpecificInfo();
    nodeDetails.cloudInfo.cloud = provider.code;
    nodeDetails.cloudInfo.instance_type = testInstanceType;
    nodeDetails.cloudInfo.region = region.code;
    nodeDetails.cloudInfo.az = az.code;
    nodeDetails.azUuid = az.uuid;
    when(mockIterator.hasNext()).thenReturn(true).thenReturn(true).thenReturn(true)
        .thenReturn(false);
    when(mockIterator.next()).thenReturn(nodeDetails); // return same node 3x
    Set<NodeDetails> mockNodeDetailsSet = mock(HashSet.class);
    when(mockNodeDetailsSet.iterator()).thenReturn(mockIterator);
    return mockNodeDetailsSet;
  }

  private UniverseDefinitionTaskParams setUpValidSSD(Iterator<NodeDetails> mockIterator) {

    // Set up instance type
    InstanceType.upsert(provider.code, testInstanceType, 10, 5.5, null);

    // Set up PriceComponent
    PriceComponent.PriceDetails instanceDetails = new PriceComponent.PriceDetails();
    instanceDetails.pricePerHour = instancePrice;
    PriceComponent.upsert(provider.code, region.code, testInstanceType, instanceDetails);

    // Set up userIntent
    UserIntent userIntent = getDummyUserIntent(getDummyDeviceInfo(numVolumes, volumeSize), provider,
        testInstanceType);

    // Set up TaskParams
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.userIntent = userIntent;
    params.nodeDetailsSet = setUpNodeDetailsSet(mockIterator);
    params.cloud = Common.CloudType.valueOf(provider.code);

    return params;
  }

  private UniverseDefinitionTaskParams setUpValidEBS(Iterator<NodeDetails> mockIterator,
                                                     DeviceInfo.EBSType ebsType) {

    // Set up instance type
    InstanceType.upsert(provider.code, testInstanceType, 10, 5.5, null);

    // Set up PriceComponents
    PriceComponent.PriceDetails instanceDetails = new PriceComponent.PriceDetails();
    instanceDetails.pricePerHour = instancePrice;
    PriceComponent.upsert(provider.code, region.code, testInstanceType, instanceDetails);
    PriceComponent.PriceDetails sizeDetails;
    switch (ebsType) {
      case IO1:
        PriceComponent.PriceDetails piopsDetails = new PriceComponent.PriceDetails();
        piopsDetails.pricePerHour = piopsPrice;
        PriceComponent.upsert(provider.code, region.code, IO1_PIOPS, piopsDetails);
        sizeDetails = new PriceComponent.PriceDetails();
        sizeDetails.pricePerHour = sizePrice;
        PriceComponent.upsert(provider.code, region.code, IO1_SIZE, sizeDetails);
        break;
      case GP2:
        sizeDetails = new PriceComponent.PriceDetails();
        sizeDetails.pricePerHour = sizePrice;
        PriceComponent.upsert(provider.code, region.code, GP2_SIZE, sizeDetails);
        break;
      default:
        break;
    }

    // Set up DeviceInfo
    DeviceInfo deviceInfo = getDummyDeviceInfo(numVolumes, volumeSize);
    deviceInfo.diskIops = diskIops;
    deviceInfo.ebsType = ebsType;

    // Set up userIntent
    UserIntent userIntent = getDummyUserIntent(deviceInfo, provider, testInstanceType);

    // Set up TaskParams
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.userIntent = userIntent;
    params.nodeDetailsSet = setUpNodeDetailsSet(mockIterator);
    params.cloud = Common.CloudType.valueOf(provider.code);

    return params;
  }

  @Before
  public void setUp() {
    provider = ModelFactory.awsProvider(ModelFactory.testCustomer());
    region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    az = AvailabilityZone.create(region, "az-1", "PlacementAZ 1", "subnet-1");
  }

  @Test
  public void testCreate() throws Exception {
    Iterator<NodeDetails> mockIterator = mock(Iterator.class);
    UniverseDefinitionTaskParams params = setUpValidSSD(mockIterator);

    // Set up mockIterator to support 2 runs throw a foreach loop
    when(mockIterator.hasNext()).thenReturn(true).thenReturn(true).thenReturn(true)
        .thenReturn(false).thenReturn(true).thenReturn(true).thenReturn(true).thenReturn(false);

    UniverseResourceDetails details = UniverseResourceDetails.create(params.nodeDetailsSet, params);
    verify(mockIterator, times(6)).next();

    assertThat(details, is(notNullValue()));
    assertThat(details.ebsPricePerHour, equalTo(0.0));
    double expectedPrice = Double.parseDouble(String.format("%.4f", 3 * instancePrice));
    assertThat(details.pricePerHour, equalTo(expectedPrice));
  }

  @Test
  public void testAddPriceToDetailsSSD() throws Exception {
    Iterator<NodeDetails> mockIterator = mock(Iterator.class);
    UniverseDefinitionTaskParams params = setUpValidSSD(mockIterator);

    UniverseResourceDetails details = new UniverseResourceDetails();
    details.addPrice(params);
    verify(mockIterator, times(3)).next();
    assertThat(details.ebsPricePerHour, equalTo(0.0));
    double expectedPrice = Double.parseDouble(String.format("%.4f", 3 * instancePrice));
    assertThat(details.pricePerHour, equalTo(expectedPrice));
  }

  @Test
  public void testAddPriceToDetailsIO1() throws Exception {
    Iterator<NodeDetails> mockIterator = mock(Iterator.class);
    UniverseDefinitionTaskParams params = setUpValidEBS(mockIterator, DeviceInfo.EBSType.IO1);

    UniverseResourceDetails details = new UniverseResourceDetails();
    details.addPrice(params);
    verify(mockIterator, times(3)).next();
    double expectedEbsPrice = Double.parseDouble(String.format("%.4f",
        3 * (numVolumes * ((diskIops * piopsPrice) + (volumeSize * sizePrice)))));
    assertThat(details.ebsPricePerHour, equalTo(expectedEbsPrice));
    double expectedPrice = Double.parseDouble(String.format("%.4f",
        expectedEbsPrice + 3 * instancePrice));
    assertThat(details.pricePerHour, equalTo(expectedPrice));
  }

  @Test
  public void testAddPriceToDetailsGP2() throws Exception {
    Iterator<NodeDetails> mockIterator = mock(Iterator.class);
    UniverseDefinitionTaskParams params = setUpValidEBS(mockIterator, DeviceInfo.EBSType.GP2);

    UniverseResourceDetails details = new UniverseResourceDetails();
    details.addPrice(params);
    verify(mockIterator, times(3)).next();
    double expectedEbsPrice = Double.parseDouble(String.format("%.4f",
        3 * numVolumes * volumeSize * sizePrice));
    assertThat(details.ebsPricePerHour, equalTo(expectedEbsPrice));
    double expectedPrice = Double.parseDouble(String.format("%.4f",
        expectedEbsPrice + 3 * instancePrice));
    assertThat(details.pricePerHour, equalTo(expectedPrice));
  }
}
