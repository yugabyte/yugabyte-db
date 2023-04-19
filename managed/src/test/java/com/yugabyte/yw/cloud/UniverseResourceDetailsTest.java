// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.cloud;

import static com.yugabyte.yw.cloud.PublicCloudConstants.GP2_SIZE;
import static com.yugabyte.yw.cloud.PublicCloudConstants.GP3_PIOPS;
import static com.yugabyte.yw.cloud.PublicCloudConstants.GP3_SIZE;
import static com.yugabyte.yw.cloud.PublicCloudConstants.GP3_THROUGHPUT;
import static com.yugabyte.yw.cloud.PublicCloudConstants.IO1_PIOPS;
import static com.yugabyte.yw.cloud.PublicCloudConstants.IO1_SIZE;
import static com.yugabyte.yw.cloud.PublicCloudConstants.StorageType;
import static com.yugabyte.yw.common.ApiUtils.getDummyDeviceInfo;
import static com.yugabyte.yw.common.ApiUtils.getDummyUserIntent;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.BeingDecommissioned;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.Decommissioned;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.Stopped;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.Terminating;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.ToBeAdded;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.ToBeRemoved;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.mockito.Mockito.mock;

import com.yugabyte.yw.cloud.UniverseResourceDetails.Context;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.PriceComponent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Iterator;
import java.util.Set;
import java.util.UUID;
import java.util.function.BiConsumer;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import org.hamcrest.CoreMatchers;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class UniverseResourceDetailsTest extends FakeDBApplication {

  private Customer customer;
  private Provider provider;
  private Region region;
  private AvailabilityZone az;
  private String testInstanceType = "c3.xlarge";
  private double instancePrice = 0.1;
  private double piopsPrice = 0.01;
  private double sizePrice = 0.01;
  private double throughputPrice = 10.24;
  private int numVolumes = 2;
  private int volumeSize = 200;
  private int diskIops = 4000;
  private int throughput = 1000;
  private NodeDetails sampleNodeDetails;
  private Context context;

  private Set<NodeDetails> setUpNodeDetailsSet() {
    return setUpNodeDetailsSet(3);
  }

  private Set<NodeDetails> setUpNodeDetailsSet(int numNodes) {
    return setUpNodeDetailsSet(numNodes, null);
  }

  private Set<NodeDetails> setUpNodeDetailsSet(
      int numNodes, BiConsumer<NodeDetails, Integer> modifier) {
    return IntStream.range(0, numNodes)
        .mapToObj(
            i -> {
              NodeDetails clone = Json.fromJson(Json.toJson(sampleNodeDetails), NodeDetails.class);
              clone.nodeIdx = i;
              if (modifier != null) {
                modifier.accept(clone, i);
              }
              return clone;
            })
        .collect(Collectors.toSet());
  }

  private UniverseDefinitionTaskParams setUpValidSSD() {
    return setUpValidSSD(3);
  }

  private UniverseDefinitionTaskParams setUpValidSSD(int numNodes) {
    return setUpValidSSD(numNodes, null);
  }

  private UniverseDefinitionTaskParams setUpValidSSD(
      int numNodes, BiConsumer<NodeDetails, Integer> modifier) {

    // Set up instance type
    InstanceType.upsert(provider.getUuid(), testInstanceType, 10, 5.5, null);

    // Set up PriceComponent
    PriceComponent.PriceDetails instanceDetails = new PriceComponent.PriceDetails();
    instanceDetails.pricePerHour = instancePrice;
    PriceComponent.upsert(provider.getUuid(), region.getCode(), testInstanceType, instanceDetails);

    // Set up userIntent
    UserIntent userIntent =
        getDummyUserIntent(getDummyDeviceInfo(numVolumes, volumeSize), provider, testInstanceType);

    // Set up TaskParams
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.upsertPrimaryCluster(userIntent, null);
    sampleNodeDetails.placementUuid = params.getPrimaryCluster().uuid;
    params.nodeDetailsSet = setUpNodeDetailsSet(numNodes, modifier);
    context = new Context(null, customer, params);
    return params;
  }

  private UniverseDefinitionTaskParams setUpValidEBS(PublicCloudConstants.StorageType storageType) {
    return setUpValidEBS(storageType, false);
  }

  private UniverseDefinitionTaskParams setUpValidEBS(
      PublicCloudConstants.StorageType storageType, boolean createEdit) {

    // Set up instance type
    InstanceType.upsert(provider.getUuid(), testInstanceType, 10, 5.5, null);

    // Set up PriceComponents
    PriceComponent.PriceDetails instanceDetails = new PriceComponent.PriceDetails();
    instanceDetails.pricePerHour = instancePrice;
    PriceComponent.upsert(provider.getUuid(), region.getCode(), testInstanceType, instanceDetails);
    PriceComponent.PriceDetails sizeDetails;
    PriceComponent.PriceDetails piopsDetails;
    PriceComponent.PriceDetails throughputDetails;
    switch (storageType) {
      case IO1:
        piopsDetails = new PriceComponent.PriceDetails();
        piopsDetails.pricePerHour = piopsPrice;
        PriceComponent.upsert(provider.getUuid(), region.getCode(), IO1_PIOPS, piopsDetails);
        sizeDetails = new PriceComponent.PriceDetails();
        sizeDetails.pricePerHour = sizePrice;
        PriceComponent.upsert(provider.getUuid(), region.getCode(), IO1_SIZE, sizeDetails);
        break;
      case GP2:
        sizeDetails = new PriceComponent.PriceDetails();
        sizeDetails.pricePerHour = sizePrice;
        PriceComponent.upsert(provider.getUuid(), region.getCode(), GP2_SIZE, sizeDetails);
        break;
      case GP3:
        piopsDetails = new PriceComponent.PriceDetails();
        piopsDetails.pricePerHour = piopsPrice;
        PriceComponent.upsert(provider.getUuid(), region.getCode(), GP3_PIOPS, piopsDetails);
        sizeDetails = new PriceComponent.PriceDetails();
        sizeDetails.pricePerHour = sizePrice;
        PriceComponent.upsert(provider.getUuid(), region.getCode(), GP3_SIZE, sizeDetails);
        throughputDetails = new PriceComponent.PriceDetails();
        throughputDetails.pricePerHour = throughputPrice;
        PriceComponent.upsert(
            provider.getUuid(), region.getCode(), GP3_THROUGHPUT, throughputDetails);
        break;
      default:
        break;
    }

    // Set up DeviceInfo
    DeviceInfo deviceInfo = getDummyDeviceInfo(numVolumes, volumeSize);
    deviceInfo.diskIops = diskIops;
    deviceInfo.throughput = throughput;
    deviceInfo.storageType = storageType;

    // Set up userIntent
    UserIntent userIntent = getDummyUserIntent(deviceInfo, provider, testInstanceType);

    // Set up TaskParams
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.upsertPrimaryCluster(userIntent, null);
    sampleNodeDetails.placementUuid = params.getPrimaryCluster().uuid;
    params.nodeDetailsSet = setUpNodeDetailsSet();

    context = new Context(null, customer, params, createEdit);
    return params;
  }

  private UniverseDefinitionTaskParams setupSamplePriceDetails(
      PublicCloudConstants.StorageType storageType) {

    // Set up instance type
    InstanceType.upsert(provider.getUuid(), testInstanceType, 10, 5.5, null);

    // Set up PriceComponents
    PriceComponent.PriceDetails sizeDetails;
    sizeDetails = new PriceComponent.PriceDetails();
    sizeDetails.pricePerHour = sizePrice;
    PriceComponent.upsert(provider.getUuid(), region.getCode(), GP2_SIZE, sizeDetails);
    PriceComponent.PriceDetails emrDetails = new PriceComponent.PriceDetails();
    emrDetails.pricePerHour = 0.68;
    PriceComponent.upsert(provider.getUuid(), region.getCode(), "c4.large", emrDetails);

    // Set up DeviceInfo
    DeviceInfo deviceInfo = getDummyDeviceInfo(numVolumes, volumeSize);
    deviceInfo.diskIops = diskIops;
    deviceInfo.throughput = throughput;
    deviceInfo.storageType = storageType;

    // Set up userIntent
    UserIntent userIntent = getDummyUserIntent(deviceInfo, provider, "c4.large");

    // Set up TaskParams
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.upsertPrimaryCluster(userIntent, null);
    sampleNodeDetails.placementUuid = params.getPrimaryCluster().uuid;
    params.nodeDetailsSet = setUpNodeDetailsSet();
    context = new Context(null, customer, params);

    return params;
  }

  private UniverseDefinitionTaskParams setupNullPriceDetails(
      PublicCloudConstants.StorageType storageType) {

    // Set up instance type
    InstanceType.upsert(provider.getUuid(), testInstanceType, 10, 5.5, null);

    // Set up null PriceComponents
    PriceComponent.upsert(provider.getUuid(), region.getCode(), GP2_SIZE, null);
    PriceComponent.upsert(provider.getUuid(), region.getCode(), "c4.large", null);

    // Set up DeviceInfo
    DeviceInfo deviceInfo = getDummyDeviceInfo(numVolumes, volumeSize);
    deviceInfo.diskIops = diskIops;
    deviceInfo.throughput = throughput;
    deviceInfo.storageType = storageType;

    // Set up userIntent
    UserIntent userIntent = getDummyUserIntent(deviceInfo, provider, "c4.large");

    // Set up TaskParams
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.upsertPrimaryCluster(userIntent, null);
    sampleNodeDetails.placementUuid = params.getPrimaryCluster().uuid;
    params.nodeDetailsSet = setUpNodeDetailsSet();
    context = new Context(null, customer, params);

    return params;
  }

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.awsProvider(customer);
    region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    az = AvailabilityZone.createOrThrow(region, "az-1", "PlacementAZ 1", "subnet-1");
    sampleNodeDetails = new NodeDetails();
    sampleNodeDetails.cloudInfo = new CloudSpecificInfo();
    sampleNodeDetails.cloudInfo.cloud = provider.getCode();
    sampleNodeDetails.cloudInfo.instance_type = testInstanceType;
    sampleNodeDetails.cloudInfo.region = region.getCode();
    sampleNodeDetails.cloudInfo.az = az.getCode();
    sampleNodeDetails.azUuid = az.getUuid();
    sampleNodeDetails.state = NodeDetails.NodeState.Live;
  }

  @Test
  public void testCreate() {
    UniverseDefinitionTaskParams params = setUpValidSSD(3);
    Context context = new Context(getApp().config(), customer, params);
    UniverseResourceDetails details =
        UniverseResourceDetails.create(params.nodeDetailsSet, params, context);

    assertThat(details, notNullValue());
    assertThat(details.ebsPricePerHour, equalTo(0.0));
    double expectedPrice = Double.parseDouble(String.format("%.4f", 3 * instancePrice));
    assertThat(details.pricePerHour, equalTo(expectedPrice));
  }

  @Test
  public void testAddPriceToDetailsSSD() {
    Iterator<NodeDetails> mockIterator = mock(Iterator.class);
    UniverseDefinitionTaskParams params = setUpValidSSD();

    UniverseResourceDetails details = new UniverseResourceDetails();
    details.addPrice(params, context);
    assertThat(details.ebsPricePerHour, equalTo(0.0));
    double expectedPrice = Double.parseDouble(String.format("%.4f", 3 * instancePrice));
    assertThat(details.pricePerHour, equalTo(expectedPrice));
  }

  @Test
  public void testAddPriceToDetailsIO1() {
    UniverseDefinitionTaskParams params = setUpValidEBS(PublicCloudConstants.StorageType.IO1);

    UniverseResourceDetails details = new UniverseResourceDetails();
    details.addPrice(params, context);
    double expectedEbsPrice =
        Double.parseDouble(
            String.format(
                "%.4f", 3 * (numVolumes * ((diskIops * piopsPrice) + (volumeSize * sizePrice)))));
    assertThat(details.ebsPricePerHour, equalTo(expectedEbsPrice));
    double expectedPrice =
        Double.parseDouble(String.format("%.4f", expectedEbsPrice + 3 * instancePrice));
    assertThat(details.pricePerHour, equalTo(expectedPrice));
  }

  @Test
  public void testAddPriceToDetailsGP2() {
    UniverseDefinitionTaskParams params = setUpValidEBS(PublicCloudConstants.StorageType.GP2);

    UniverseResourceDetails details = new UniverseResourceDetails();
    details.addPrice(params, context);
    double expectedEbsPrice =
        Double.parseDouble(String.format("%.4f", 3 * numVolumes * volumeSize * sizePrice));
    assertThat(details.ebsPricePerHour, equalTo(expectedEbsPrice));
    double expectedPrice =
        Double.parseDouble(String.format("%.4f", expectedEbsPrice + 3 * instancePrice));
    assertThat(details.pricePerHour, equalTo(expectedPrice));
  }

  @Test
  public void testAddPriceToDetailsGP3() {
    UniverseDefinitionTaskParams params = setUpValidEBS(StorageType.GP3);

    UniverseResourceDetails details = new UniverseResourceDetails();
    details.gp3FreePiops = 3000;
    details.gp3FreeThroughput = 125;
    details.addPrice(params, context);
    double expectedEbsPrice =
        Double.parseDouble(
            String.format(
                "%.4f",
                3
                    * (numVolumes
                        * (((diskIops - 3000) * piopsPrice)
                            + (volumeSize * sizePrice)
                            + ((throughput - 125) * throughputPrice / 1024)))));
    assertThat(details.ebsPricePerHour, equalTo(expectedEbsPrice));
    double expectedPrice =
        Double.parseDouble(String.format("%.4f", expectedEbsPrice + 3 * instancePrice));
    assertThat(details.pricePerHour, equalTo(expectedPrice));
  }

  @Test
  public void testAddPricePausedUniverse() {
    UniverseDefinitionTaskParams params = setUpValidEBS(StorageType.GP3);
    params.universePaused = true;
    params.nodeDetailsSet.forEach(node -> node.state = Stopped);

    UniverseResourceDetails details = new UniverseResourceDetails();
    details.gp3FreePiops = 3000;
    details.gp3FreeThroughput = 125;
    details.addPrice(params, context);
    double expectedEbsPrice =
        Double.parseDouble(
            String.format(
                "%.4f",
                3
                    * (numVolumes
                        * (((diskIops - 3000) * piopsPrice)
                            + (volumeSize * sizePrice)
                            + ((throughput - 125) * throughputPrice / 1024)))));
    // We're only accounted for drives in case universe is paused.
    assertThat(details.ebsPricePerHour, equalTo(expectedEbsPrice));
    assertThat(details.pricePerHour, equalTo(expectedEbsPrice));
  }

  @Test
  public void testAddPriceWithTerminatedNode() {
    UniverseDefinitionTaskParams params =
        setUpValidSSD(
            4,
            (node, index) -> {
              if (index == 2) {
                node.state = Terminating;
              }
              if (index == 3) {
                node.state = Terminating;
              }
            });

    UniverseResourceDetails details = new UniverseResourceDetails();
    details.addPrice(params, context);
    assertThat(details.ebsPricePerHour, equalTo(0.0));
    double expectedPrice = Double.parseDouble(String.format("%.4f", 2 * instancePrice));
    assertThat(details.pricePerHour, equalTo(expectedPrice));
  }

  @Test
  public void testAddPriceWithDecommissionedNode() {
    UniverseDefinitionTaskParams params =
        setUpValidSSD(
            4,
            (node, index) -> {
              if (index == 2) {
                node.state = Decommissioned;
              }
              if (index == 3) {
                node.state = BeingDecommissioned;
              }
            });

    UniverseResourceDetails details = new UniverseResourceDetails();
    details.addPrice(params, context);
    assertThat(details.ebsPricePerHour, equalTo(0.0));
    double expectedPrice = Double.parseDouble(String.format("%.4f", 2 * instancePrice));
    assertThat(details.pricePerHour, equalTo(expectedPrice));
  }

  @Test
  public void testAddPriceWithToBeAdded() {
    // ToBeAdded nodes are not counted - as node is not created yet
    UniverseDefinitionTaskParams params = setUpValidEBS(StorageType.GP3);
    params.nodeDetailsSet.forEach(node -> node.state = ToBeAdded);

    UniverseResourceDetails details = new UniverseResourceDetails();
    details.gp3FreePiops = 3000;
    details.gp3FreeThroughput = 125;
    details.addPrice(params, context);
    assertThat(details.ebsPricePerHour, equalTo(0.0));
    assertThat(details.pricePerHour, equalTo(0.0));
  }

  @Test
  public void testAddPriceWithToBeRemoved() {
    // ToBeRemoved nodes are not counted - as node is still running
    UniverseDefinitionTaskParams params = setUpValidEBS(StorageType.GP3);
    params.nodeDetailsSet.forEach(node -> node.state = ToBeRemoved);

    UniverseResourceDetails details = new UniverseResourceDetails();
    details.gp3FreePiops = 3000;
    details.gp3FreeThroughput = 125;
    details.addPrice(params, context);
    double expectedEbsPrice =
        Double.parseDouble(
            String.format(
                "%.4f",
                3
                    * (numVolumes
                        * (((diskIops - 3000) * piopsPrice)
                            + (volumeSize * sizePrice)
                            + ((throughput - 125) * throughputPrice / 1024)))));
    assertThat(details.ebsPricePerHour, equalTo(expectedEbsPrice));
    double expectedPrice =
        Double.parseDouble(String.format("%.4f", 3 * instancePrice + expectedEbsPrice));
    assertThat(details.pricePerHour, equalTo(expectedPrice));
  }

  @Test
  public void testAddPriceWithToBeAddedOnCreateEdit() {
    // ToBeAdded nodes are counted during create or edit - to show estimate for future universe
    UniverseDefinitionTaskParams params = setUpValidEBS(StorageType.GP3, true);
    params.nodeDetailsSet.forEach(node -> node.state = ToBeAdded);

    UniverseResourceDetails details = new UniverseResourceDetails();
    details.gp3FreePiops = 3000;
    details.gp3FreeThroughput = 125;
    details.addPrice(params, context);
    double expectedEbsPrice =
        Double.parseDouble(
            String.format(
                "%.4f",
                3
                    * (numVolumes
                        * (((diskIops - 3000) * piopsPrice)
                            + (volumeSize * sizePrice)
                            + ((throughput - 125) * throughputPrice / 1024)))));
    assertThat(details.ebsPricePerHour, equalTo(expectedEbsPrice));
    double expectedPrice =
        Double.parseDouble(String.format("%.4f", 3 * instancePrice + expectedEbsPrice));
    assertThat(details.pricePerHour, equalTo(expectedPrice));
  }

  @Test
  public void testAddPriceWithToBeRemovedOnCreateEdit() {
    // ToBeRemoved nodes are not counted during create or edit
    // - to show estimate for future universe
    UniverseDefinitionTaskParams params = setUpValidEBS(StorageType.GP3, true);
    params.nodeDetailsSet.forEach(node -> node.state = ToBeRemoved);

    UniverseResourceDetails details = new UniverseResourceDetails();
    details.gp3FreePiops = 3000;
    details.gp3FreeThroughput = 125;
    details.addPrice(params, context);
    assertThat(details.ebsPricePerHour, equalTo(0.0));
    assertThat(details.pricePerHour, equalTo(0.0));
  }

  @Test
  public void testAddCustomPriceDetails() {
    UniverseDefinitionTaskParams params =
        setupSamplePriceDetails(PublicCloudConstants.StorageType.GP2);
    params.getPrimaryCluster().userIntent.instanceType = "c4.large";
    UniverseResourceDetails details = new UniverseResourceDetails();
    details.addPrice(params, context);
    double expectedEbsPrice =
        Double.parseDouble(String.format("%.4f", 3 * numVolumes * volumeSize * sizePrice));
    assertThat(details.ebsPricePerHour, equalTo(expectedEbsPrice));
    double expectedPrice = Double.parseDouble(String.format("%.4f", expectedEbsPrice + 3 * 0.68));
    assertThat(details.pricePerHour, equalTo(expectedPrice));
  }

  @Test
  public void testAddNullPriceDetails() {
    UniverseDefinitionTaskParams params =
        setupNullPriceDetails(PublicCloudConstants.StorageType.GP2);
    params.getPrimaryCluster().userIntent.instanceType = "c4.large";
    UniverseResourceDetails details = new UniverseResourceDetails();
    details.addPrice(params, context);
    assertThat(details.ebsPricePerHour, equalTo(0.0));
    assertThat(details.pricePerHour, equalTo(0.0));
  }

  @Test
  public void testRRWithDiffProvider() {
    InstanceType.upsert(provider.getUuid(), testInstanceType, 10, 5.5, null);
    Provider provider2 = ModelFactory.gcpProvider(customer);
    String testInstanceType2 = "c4.large";
    InstanceType.upsert(provider2.getUuid(), testInstanceType2, 5, 5.0, null);

    // Set up PriceComponent
    PriceComponent.PriceDetails instanceDetails = new PriceComponent.PriceDetails();
    instanceDetails.pricePerHour = instancePrice;
    PriceComponent.upsert(provider.getUuid(), region.getCode(), testInstanceType, instanceDetails);

    // Set up userIntent
    UserIntent userIntent =
        getDummyUserIntent(getDummyDeviceInfo(numVolumes, volumeSize), provider, testInstanceType);

    UserIntent rrIntent =
        getDummyUserIntent(
            getDummyDeviceInfo(numVolumes, volumeSize), provider2, testInstanceType2);

    // Set up TaskParams
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.upsertPrimaryCluster(userIntent, null);
    sampleNodeDetails.placementUuid = params.getPrimaryCluster().uuid;
    params.nodeDetailsSet = setUpNodeDetailsSet(2, null);
    params.upsertCluster(rrIntent, null, UUID.randomUUID());
    NodeDetails nodeDetails = new NodeDetails();
    nodeDetails.cloudInfo = new CloudSpecificInfo();
    nodeDetails.cloudInfo.cloud = provider2.getCode();
    nodeDetails.cloudInfo.instance_type = testInstanceType2;
    nodeDetails.cloudInfo.region = region.getCode();
    nodeDetails.cloudInfo.az = az.getCode();
    nodeDetails.azUuid = az.getUuid();
    nodeDetails.state = NodeDetails.NodeState.Live;
    nodeDetails.placementUuid = params.getReadOnlyClusters().get(0).uuid;
    params.nodeDetailsSet.add(nodeDetails);

    context = new Context(null, customer, params);
    InstanceType type1 = context.getInstanceType(provider.getUuid(), testInstanceType);
    InstanceType type2 = context.getInstanceType(provider2.getUuid(), testInstanceType2);

    assertThat(type1, notNullValue());
    assertThat(type2, notNullValue());
    assertThat(type1, CoreMatchers.not(equalTo(type2)));
  }
}
