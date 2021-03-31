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
import org.mockito.stubbing.OngoingStubbing;
import play.libs.Json;

import java.util.HashSet;
import java.util.Iterator;
import java.util.Set;

import static com.yugabyte.yw.cloud.PublicCloudConstants.GP2_SIZE;
import static com.yugabyte.yw.cloud.PublicCloudConstants.IO1_PIOPS;
import static com.yugabyte.yw.cloud.PublicCloudConstants.IO1_SIZE;
import static com.yugabyte.yw.common.ApiUtils.getDummyDeviceInfo;
import static com.yugabyte.yw.common.ApiUtils.getDummyUserIntent;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.ToBeRemoved;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertThat;
import static org.mockito.Mockito.mock;
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
  private NodeDetails sampleNodeDetails;

  private Set<NodeDetails> setUpNodeDetailsSet(Iterator<NodeDetails> mockIterator) {
    return setUpNodeDetailsSet(mockIterator, 3);
  }

  private Set<NodeDetails> setUpNodeDetailsSet(Iterator<NodeDetails> mockIterator,
                                               int numIterations) {
    OngoingStubbing hasNextStubbing = when(mockIterator.hasNext());
    for (int i = 0; i < numIterations; ++i) {
      hasNextStubbing = hasNextStubbing.thenReturn(true);
    }
    hasNextStubbing.thenReturn(false);
    OngoingStubbing nextStubbing = when(mockIterator.next());
    for (int i = 0; i < numIterations; ++i) {
      nextStubbing = nextStubbing.thenReturn(sampleNodeDetails); // return same node 3x
    }
    nextStubbing.thenReturn(null);
    Set<NodeDetails> mockNodeDetailsSet = mock(HashSet.class);
    when(mockNodeDetailsSet.iterator()).thenReturn(mockIterator);
    return mockNodeDetailsSet;
  }

  private UniverseDefinitionTaskParams setUpValidSSD(Iterator<NodeDetails> mockIterator) {
    return setUpValidSSD(mockIterator, 3);
  }

  private UniverseDefinitionTaskParams setUpValidSSD(Iterator<NodeDetails> mockIterator,
                                                     int numIterations) {

    // Set up instance type
    InstanceType.upsert(provider.uuid, testInstanceType, 10, 5.5, null);

    // Set up PriceComponent
    PriceComponent.PriceDetails instanceDetails = new PriceComponent.PriceDetails();
    instanceDetails.pricePerHour = instancePrice;
    PriceComponent.upsert(provider.uuid, region.code, testInstanceType, instanceDetails);

    // Set up userIntent
    UserIntent userIntent = getDummyUserIntent(getDummyDeviceInfo(numVolumes, volumeSize), provider,
        testInstanceType);

    // Set up TaskParams
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.upsertPrimaryCluster(userIntent, null);
    sampleNodeDetails.placementUuid = params.getPrimaryCluster().uuid;
    params.nodeDetailsSet = setUpNodeDetailsSet(mockIterator, numIterations);
    return params;
  }

  private UniverseDefinitionTaskParams setUpValidEBS(Iterator<NodeDetails> mockIterator,
                                                     PublicCloudConstants.StorageType storageType) {

    // Set up instance type
    InstanceType.upsert(provider.uuid, testInstanceType, 10, 5.5, null);

    // Set up PriceComponents
    PriceComponent.PriceDetails instanceDetails = new PriceComponent.PriceDetails();
    instanceDetails.pricePerHour = instancePrice;
    PriceComponent.upsert(provider.uuid, region.code, testInstanceType, instanceDetails);
    PriceComponent.PriceDetails sizeDetails;
    switch (storageType) {
      case IO1:
        PriceComponent.PriceDetails piopsDetails = new PriceComponent.PriceDetails();
        piopsDetails.pricePerHour = piopsPrice;
        PriceComponent.upsert(provider.uuid, region.code, IO1_PIOPS, piopsDetails);
        sizeDetails = new PriceComponent.PriceDetails();
        sizeDetails.pricePerHour = sizePrice;
        PriceComponent.upsert(provider.uuid, region.code, IO1_SIZE, sizeDetails);
        break;
      case GP2:
        sizeDetails = new PriceComponent.PriceDetails();
        sizeDetails.pricePerHour = sizePrice;
        PriceComponent.upsert(provider.uuid, region.code, GP2_SIZE, sizeDetails);
        break;
      default:
        break;
    }

    // Set up DeviceInfo
    DeviceInfo deviceInfo = getDummyDeviceInfo(numVolumes, volumeSize);
    deviceInfo.diskIops = diskIops;
    deviceInfo.storageType = storageType;

    // Set up userIntent
    UserIntent userIntent = getDummyUserIntent(deviceInfo, provider, testInstanceType);

    // Set up TaskParams
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.upsertPrimaryCluster(userIntent, null);
    sampleNodeDetails.placementUuid = params.getPrimaryCluster().uuid;
    params.nodeDetailsSet = setUpNodeDetailsSet(mockIterator);

    return params;
  }

  private UniverseDefinitionTaskParams setupSamplePriceDetails(Iterator<NodeDetails> mockIterator,
                                                     PublicCloudConstants.StorageType storageType) {

    // Set up instance type
    InstanceType.upsert(provider.uuid, testInstanceType, 10, 5.5, null);

    // Set up PriceComponents
    PriceComponent.PriceDetails sizeDetails;
    sizeDetails = new PriceComponent.PriceDetails();
    sizeDetails.pricePerHour = sizePrice;
    PriceComponent.upsert(provider.uuid, region.code, GP2_SIZE, sizeDetails);
    PriceComponent.PriceDetails emrDetails = new PriceComponent.PriceDetails();
    emrDetails.pricePerHour = 0.68;
    PriceComponent.upsert(provider.uuid, region.code, "c4.large", emrDetails);

    // Set up DeviceInfo
    DeviceInfo deviceInfo = getDummyDeviceInfo(numVolumes, volumeSize);
    deviceInfo.diskIops = diskIops;
    deviceInfo.storageType = storageType;

    // Set up userIntent
    UserIntent userIntent = getDummyUserIntent(deviceInfo, provider, "c4.large");

    // Set up TaskParams
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.upsertPrimaryCluster(userIntent, null);
    sampleNodeDetails.placementUuid = params.getPrimaryCluster().uuid;
    params.nodeDetailsSet = setUpNodeDetailsSet(mockIterator);

    return params;
  }

  private UniverseDefinitionTaskParams setupNullPriceDetails(Iterator<NodeDetails> mockIterator,
                                                               PublicCloudConstants.StorageType storageType) {

    // Set up instance type
    InstanceType.upsert(provider.uuid, testInstanceType, 10, 5.5, null);

    // Set up null PriceComponents
    PriceComponent.upsert(provider.uuid, region.code, GP2_SIZE, null);
    PriceComponent.upsert(provider.uuid, region.code, "c4.large", null);

    // Set up DeviceInfo
    DeviceInfo deviceInfo = getDummyDeviceInfo(numVolumes, volumeSize);
    deviceInfo.diskIops = diskIops;
    deviceInfo.storageType = storageType;

    // Set up userIntent
    UserIntent userIntent = getDummyUserIntent(deviceInfo, provider, "c4.large");

    // Set up TaskParams
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.upsertPrimaryCluster(userIntent, null);
    sampleNodeDetails.placementUuid = params.getPrimaryCluster().uuid;
    params.nodeDetailsSet = setUpNodeDetailsSet(mockIterator);

    return params;
  }

  @Before
  public void setUp() {
    provider = ModelFactory.awsProvider(ModelFactory.testCustomer());
    region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    az = AvailabilityZone.create(region, "az-1", "PlacementAZ 1", "subnet-1");
    sampleNodeDetails = new NodeDetails();
    sampleNodeDetails.cloudInfo = new CloudSpecificInfo();
    sampleNodeDetails.cloudInfo.cloud = provider.code;
    sampleNodeDetails.cloudInfo.instance_type = testInstanceType;
    sampleNodeDetails.cloudInfo.region = region.code;
    sampleNodeDetails.cloudInfo.az = az.code;
    sampleNodeDetails.azUuid = az.uuid;
    sampleNodeDetails.state = NodeDetails.NodeState.Live;
  }

  @Test
  public void testCreate() throws Exception {
    Iterator<NodeDetails> mockIterator = mock(Iterator.class);
    UniverseDefinitionTaskParams params = setUpValidSSD(mockIterator, 6);

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
    UniverseDefinitionTaskParams params = setUpValidEBS(mockIterator,
        PublicCloudConstants.StorageType.IO1);

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
    UniverseDefinitionTaskParams params = setUpValidEBS(mockIterator,
        PublicCloudConstants.StorageType.GP2);

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

  @Test
  public void testAddPriceWithRemovingOneNode() throws Exception {
    NodeDetails decommissioningNode = Json.fromJson(Json.toJson(sampleNodeDetails).deepCopy(),
        NodeDetails.class);
    decommissioningNode.state = ToBeRemoved;
    Iterator<NodeDetails> mockIterator = mock(Iterator.class);
    UniverseDefinitionTaskParams params = setUpValidSSD(mockIterator, 4);
    OngoingStubbing nextStubbing = when(mockIterator.next());
    for (int i = 0; i < 3; ++i) {
      nextStubbing = nextStubbing.thenReturn(sampleNodeDetails); // return same node 3x
    }
    decommissioningNode.placementUuid = sampleNodeDetails.placementUuid;
    nextStubbing = nextStubbing.thenReturn(decommissioningNode);
    nextStubbing.thenReturn(null);

    UniverseResourceDetails details = new UniverseResourceDetails();
    details.addPrice(params);
    verify(mockIterator, times(4)).next();
    assertThat(details.ebsPricePerHour, equalTo(0.0));
    double expectedPrice = Double.parseDouble(String.format("%.4f", 3 * instancePrice));
    assertThat(details.pricePerHour, equalTo(expectedPrice));
  }

  @Test
  public void testAddCustomPriceDetails() {
    Iterator<NodeDetails> mockIterator = mock(Iterator.class);
    UniverseDefinitionTaskParams params = setupSamplePriceDetails(mockIterator,
            PublicCloudConstants.StorageType.GP2);
    params.getPrimaryCluster().userIntent.instanceType = "c4.large";
    UniverseResourceDetails details = new UniverseResourceDetails();
    details.addPrice(params);
    verify(mockIterator, times(3)).next();
    double expectedEbsPrice = Double.parseDouble(String.format("%.4f",
            3 * numVolumes * volumeSize * sizePrice));
    assertThat(details.ebsPricePerHour, equalTo(expectedEbsPrice));
    double expectedPrice = Double.parseDouble(String.format("%.4f",
            expectedEbsPrice + 3 * 0.68));
    assertThat(details.pricePerHour, equalTo(expectedPrice));
  }

  @Test
  public void testAddNullPriceDetails() {
    Iterator<NodeDetails> mockIterator = mock(Iterator.class);
    UniverseDefinitionTaskParams params =  setupNullPriceDetails(mockIterator,
            PublicCloudConstants.StorageType.GP2);
    params.getPrimaryCluster().userIntent.instanceType = "c4.large";
    UniverseResourceDetails details = new UniverseResourceDetails();
    details.addPrice(params);
    verify(mockIterator, times(3)).next();
    assertThat(details.ebsPricePerHour, equalTo(0.0));
    assertThat(details.pricePerHour, equalTo(0.0));
  }
}
