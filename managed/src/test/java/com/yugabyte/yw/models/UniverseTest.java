// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Sets;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.cloud.UniverseResourceDetails;
import com.yugabyte.yw.cloud.UniverseResourceDetails.Context;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.models.helpers.AllowedActionsHelper;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.apache.commons.lang3.StringUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class UniverseTest extends FakeDBApplication {
  private Provider defaultProvider;
  private Customer defaultCustomer;
  private CertificateHelper certificateHelper;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
    certificateHelper = new CertificateHelper(app.injector().instanceOf(RuntimeConfGetter.class));
  }

  @Test
  public void testCreate() {
    Universe u = createUniverse(defaultCustomer.getId());
    assertNotNull(u);
    assertThat(u.getUniverseUUID(), is(allOf(notNullValue(), equalTo(u.getUniverseUUID()))));
    assertThat(u.getVersion(), is(allOf(notNullValue(), equalTo(1))));
    assertThat(u.getName(), is(allOf(notNullValue(), equalTo("Test Universe"))));
    assertThat(u.getUniverseDetails(), is(notNullValue()));
  }

  @Test
  public void testConfig() {
    Universe u = createUniverse(defaultCustomer.getId());
    assertNotNull(u);
    Map<String, String> config = new HashMap<>();
    config.put(Universe.TAKE_BACKUPS, "true");
    u.updateConfig(config);
    u.save();
    assertEquals(config, u.getConfig());
  }

  @Test
  public void testGetSingleUniverse() {
    Universe newUniverse = createUniverse(defaultCustomer.getId());
    assertNotNull(newUniverse);
    Universe fetchedUniverse = Universe.getOrBadRequest(newUniverse.getUniverseUUID());
    assertNotNull(fetchedUniverse);
    assertEquals(fetchedUniverse, newUniverse);
  }

  @Test
  public void testCheckIfUniverseExists() {
    Universe newUniverse = createUniverse(defaultCustomer.getId());
    assertNotNull(newUniverse);
    assertThat(Universe.checkIfUniverseExists("Test Universe"), equalTo(true));
    assertThat(Universe.checkIfUniverseExists("Fake Universe"), equalTo(false));
  }

  @Test
  public void testGetMultipleUniverse() {
    Universe u1 = createUniverse("Universe1", defaultCustomer.getId());
    Universe u2 = createUniverse("Universe2", defaultCustomer.getId());
    Universe u3 = createUniverse("Universe3", defaultCustomer.getId());
    Set<UUID> uuids =
        Sets.newHashSet(u1.getUniverseUUID(), u2.getUniverseUUID(), u3.getUniverseUUID());

    Set<Universe> universes = Universe.getAllPresent(uuids);
    assertNotNull(universes);
    assertEquals(universes.size(), 3);
  }

  @Test(expected = RuntimeException.class)
  public void testGetUnknownUniverse() {
    UUID unknownUUID = UUID.randomUUID();
    Universe.getOrBadRequest(unknownUUID);
  }

  @Test
  public void testParallelSaveDetails() {
    int numNodes = 100;
    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("TaskPool-%d").build();
    ThreadPoolExecutor executor =
        new ThreadPoolExecutor(
            numNodes,
            numNodes,
            60L,
            TimeUnit.SECONDS,
            new LinkedBlockingQueue<>(),
            namedThreadFactory);
    Universe u = createUniverse(defaultCustomer.getId());
    assertEquals(0, u.getNodes().size());
    for (int i = 0; i < numNodes; i++) {
      SaveNode sn = new SaveNode(u.getUniverseUUID(), i);
      executor.execute(sn);
    }
    executor.shutdown();
    try {
      executor.awaitTermination(120, TimeUnit.SECONDS);
    } catch (InterruptedException e) {
      fail();
    }
    Universe updUniv = Universe.getOrBadRequest(u.getUniverseUUID());
    assertEquals(numNodes, updUniv.getNodes().size());
    assertEquals(numNodes + 1, updUniv.getVersion());
  }

  @Test
  public void testSaveDetails() {
    Universe u = createUniverse(defaultCustomer.getId());
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails;
          universeDetails = new UniverseDefinitionTaskParams();
          UserIntent userIntent = new UserIntent();

          // Create some subnets.
          List<String> subnets = new ArrayList<>();
          subnets.add("subnet-1");
          subnets.add("subnet-2");
          subnets.add("subnet-3");

          // Add a desired number of nodes.
          userIntent.numNodes = 5;
          universeDetails.nodeDetailsSet = new HashSet<>();
          for (int idx = 1; idx <= userIntent.numNodes; idx++) {
            NodeDetails node = new NodeDetails();
            node.nodeName = "host-n" + idx;
            node.cloudInfo = new CloudSpecificInfo();
            node.cloudInfo.cloud = "aws";
            node.cloudInfo.az = "az-" + idx;
            node.cloudInfo.region = "test-region";
            node.cloudInfo.subnet_id = subnets.get(idx % subnets.size());
            node.cloudInfo.private_ip = "host-n" + idx;
            node.state = NodeState.Live;
            node.isTserver = true;
            if (idx <= 3) {
              node.isMaster = true;
            }
            node.nodeIdx = idx;
            universeDetails.nodeDetailsSet.add(node);
          }
          universeDetails.upsertPrimaryCluster(userIntent, null);
          universe.setUniverseDetails(universeDetails);
        };
    u = Universe.saveDetails(u.getUniverseUUID(), updater);

    int nodeIdx;
    for (NodeDetails node : u.getMasters()) {
      assertTrue(node.isMaster);
      assertNotNull(node.nodeName);
      nodeIdx = Character.getNumericValue(node.nodeName.charAt(node.nodeName.length() - 1));
      assertTrue(nodeIdx <= 3);
    }

    for (NodeDetails node : u.getTServers()) {
      assertTrue(node.isTserver);
      assertNotNull(node.nodeName);
      nodeIdx = Character.getNumericValue(node.nodeName.charAt(node.nodeName.length() - 1));
      assertTrue(nodeIdx <= 5);
    }

    assertTrue(u.getTServers().size() > u.getMasters().size());
    assertEquals(u.getMasters().size(), 3);
    assertEquals(u.getTServers().size(), 5);
  }

  @Test
  public void testVerifyIsTrue() {
    Universe u = createUniverse(defaultCustomer.getId());
    List<NodeDetails> masters = new LinkedList<>();
    NodeDetails mockNode1 = mock(NodeDetails.class);
    masters.add(mockNode1);
    NodeDetails mockNode2 = mock(NodeDetails.class);
    masters.add(mockNode2);
    when(mockNode1.isQueryable()).thenReturn(true);
    when(mockNode2.isQueryable()).thenReturn(true);
    assertTrue(u.verifyMastersAreQueryable(masters));
  }

  @Test
  public void testMastersListEmptyVerifyIsFalse() {
    Universe u = createUniverse(defaultCustomer.getId());
    assertFalse(u.verifyMastersAreQueryable(null));
    List<NodeDetails> masters = new LinkedList<>();
    assertFalse(u.verifyMastersAreQueryable(masters));
  }

  @Test
  public void testMastersInBadStateVerifyIsFalse() {
    Universe u = createUniverse(defaultCustomer.getId());
    List<NodeDetails> masters = new LinkedList<>();
    NodeDetails mockNode1 = mock(NodeDetails.class);
    masters.add(mockNode1);
    NodeDetails mockNode2 = mock(NodeDetails.class);
    masters.add(mockNode2);
    when(mockNode1.isQueryable()).thenReturn(false);
    when(mockNode2.isQueryable()).thenReturn(true);
    assertFalse(u.verifyMastersAreQueryable(masters));
    when(mockNode1.isQueryable()).thenReturn(true);
    when(mockNode2.isQueryable()).thenReturn(false);
    assertFalse(u.verifyMastersAreQueryable(masters));
  }

  @Test
  public void testGetMasterAddresses() {
    Universe u = createUniverse(defaultCustomer.getId());

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails;
          universeDetails = new UniverseDefinitionTaskParams();
          UserIntent userIntent = new UserIntent();

          // Add a desired number of nodes.
          userIntent.numNodes = 3;
          universeDetails.nodeDetailsSet = new HashSet<>();
          for (int idx = 1; idx <= userIntent.numNodes; idx++) {
            NodeDetails node = new NodeDetails();
            node.nodeName = "host-n" + idx;
            node.cloudInfo = new CloudSpecificInfo();
            node.cloudInfo.cloud = "aws";
            node.cloudInfo.az = "az-" + idx;
            node.cloudInfo.region = "test-region";
            node.cloudInfo.subnet_id = "subnet-" + idx;
            node.cloudInfo.private_ip = "host-n" + idx;
            node.state = NodeState.Live;
            node.isTserver = true;
            if (idx <= 3) {
              node.isMaster = true;
            }
            node.nodeIdx = idx;
            universeDetails.upsertPrimaryCluster(userIntent, null);
            universeDetails.nodeDetailsSet.add(node);
          }
          universe.setUniverseDetails(universeDetails);
        };
    u = Universe.saveDetails(u.getUniverseUUID(), updater);
    String masterAddrs = u.getMasterAddresses();
    assertNotNull(masterAddrs);
    for (int idx = 1; idx <= 3; idx++) {
      assertThat(masterAddrs, containsString("host-n" + idx));
    }
  }

  @Test
  public void testGetMasterAddressesFails() {
    Universe u = spy(createUniverse(defaultCustomer.getId()));
    when(u.verifyMastersAreQueryable(anyList())).thenReturn(false);
    assertEquals("", u.getMasterAddresses());
  }

  @Test
  public void testToJSONSuccess() {
    Universe u = createUniverse(defaultCustomer.getId());
    Map<String, String> universeParams = new HashMap<>();
    universeParams.put(Universe.TAKE_BACKUPS, "true");
    u.updateConfig(universeParams);
    u.save();

    // Create regions
    Region r1 = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    Region r2 = Region.create(defaultProvider, "region-2", "Region 2", "yb-image-1");
    Region r3 = Region.create(defaultProvider, "region-3", "Region 3", "yb-image-1");
    AvailabilityZone.createOrThrow(r1, "az-1", "AZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r2, "az-2", "AZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(r3, "az-3", "AZ 3", "subnet-3");
    List<UUID> regionList = new ArrayList<>();
    regionList.add(r1.getUuid());
    regionList.add(r2.getUuid());
    regionList.add(r3.getUuid());

    // Add non-EBS instance type with price to each region
    String instanceType = "c3.xlarge";
    double instancePrice = 0.1;
    InstanceType.upsert(defaultProvider.getUuid(), instanceType, 1, 20.0, null);
    PriceComponent.PriceDetails instanceDetails = new PriceComponent.PriceDetails();
    instanceDetails.pricePerHour = instancePrice;
    PriceComponent.upsert(defaultProvider.getUuid(), r1.getCode(), instanceType, instanceDetails);
    PriceComponent.upsert(defaultProvider.getUuid(), r2.getCode(), instanceType, instanceDetails);
    PriceComponent.upsert(defaultProvider.getUuid(), r3.getCode(), instanceType, instanceDetails);

    // Create userIntent
    UserIntent userIntent = new UserIntent();
    userIntent.replicationFactor = 3;
    userIntent.regionList = regionList;
    userIntent.instanceType = instanceType;
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.storageType = PublicCloudConstants.StorageType.IO1;
    userIntent.deviceInfo.numVolumes = 2;
    userIntent.deviceInfo.diskIops = 1000;
    userIntent.deviceInfo.volumeSize = 100;

    u = Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent));
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.getPrimaryCluster().regions = ImmutableList.of(r1, r2, r3);
              universe.setUniverseDetails(universeDetails);
            });

    Context context = new Context(getApp().config(), u);
    UniverseResourceDetails resourceDetails =
        UniverseResourceDetails.create(u.getUniverseDetails(), context);
    JsonNode universeJson = Json.toJson(new UniverseResp(u, null, resourceDetails, null, null));
    assertThat(
        universeJson.get("universeUUID").asText(),
        allOf(notNullValue(), equalTo(u.getUniverseUUID().toString())));
    assertThat(
        universeJson.get("resources").asText(),
        allOf(notNullValue(), equalTo(Json.toJson(resourceDetails).asText())));
    JsonNode universeConfig = universeJson.get("universeConfig");
    assertEquals(universeConfig.toString(), "{\"takeBackups\":\"true\"}");
    JsonNode clustersListJson = universeJson.get("universeDetails").get("clusters");
    assertThat(clustersListJson, notNullValue());
    assertTrue(clustersListJson.isArray());
    assertEquals(1, clustersListJson.size());
    JsonNode clusterJson = clustersListJson.get(0);
    JsonNode userIntentJson = clusterJson.get("userIntent");
    assertTrue(userIntentJson.get("regionList").isArray());
    assertEquals(3, userIntentJson.get("regionList").size());
    JsonNode masterGFlags = userIntentJson.get("masterGFlags");
    assertThat(masterGFlags, is(notNullValue()));
    assertTrue(masterGFlags.isObject());

    JsonNode providerNode = userIntentJson.get("provider");
    assertThat(providerNode, notNullValue());
    assertThat(
        providerNode.asText(),
        allOf(notNullValue(), equalTo(defaultProvider.getUuid().toString())));

    JsonNode regionsNode = clusterJson.get("regions");
    assertThat(regionsNode, is(notNullValue()));
    assertTrue(regionsNode.isArray());
    assertEquals(3, regionsNode.size());
    assertNull(universeJson.get("dnsName"));
  }

  @Test
  public void testToJSONWithNullRegionList() {
    Universe u = createUniverse(defaultCustomer.getId());
    u = Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    UserIntent ui = u.getUniverseDetails().getPrimaryCluster().userIntent;
    ui.provider =
        Provider.get(defaultCustomer.getUuid(), CloudType.aws).get(0).getUuid().toString();
    u.getUniverseDetails().upsertPrimaryCluster(ui, null);

    JsonNode universeJson = Json.toJson(new UniverseResp(u, null));
    assertThat(
        universeJson.get("universeUUID").asText(),
        allOf(notNullValue(), equalTo(u.getUniverseUUID().toString())));
    JsonNode clusterJson = universeJson.get("universeDetails").get("clusters").get(0);
    assertTrue(!clusterJson.get("userIntent").has("regionList"));
    assertNull(clusterJson.get("regions"));
    assertNull(clusterJson.get("provider"));
  }

  @Test
  public void testToJSONWithNullGFlags() {
    Universe u = createUniverse(defaultCustomer.getId());
    UserIntent userIntent = new UserIntent();
    userIntent.replicationFactor = 3;
    userIntent.regionList = new ArrayList<>();
    userIntent.masterGFlags = null;
    userIntent.provider =
        Provider.get(defaultCustomer.getUuid(), CloudType.aws).get(0).getUuid().toString();

    // SaveDetails in order to generate universeDetailsJson with null gflags
    u = Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent));

    // Update in-memory user intent so userDetails no longer has null gflags, but json still does
    UniverseDefinitionTaskParams udtp = u.getUniverseDetails();
    udtp.getPrimaryCluster().userIntent.masterGFlags = new HashMap<>();
    u.setUniverseDetails(udtp);

    // Verify returned json is generated from the non-json userDetails object
    JsonNode universeJson = Json.toJson(new UniverseResp(u, null));
    assertThat(
        universeJson.get("universeUUID").asText(),
        allOf(notNullValue(), equalTo(u.getUniverseUUID().toString())));
    JsonNode clusterJson = universeJson.get("universeDetails").get("clusters").get(0);
    JsonNode masterGFlags = clusterJson.get("userIntent").get("masterGFlags");
    assertThat(masterGFlags, is(notNullValue()));
    assertTrue(masterGFlags.isObject());
    JsonNode providerNode = universeJson.get("provider");
    assertNull(providerNode);
  }

  @Test
  public void testToJSONWithEmptyRegionList() {
    Universe u = createUniverseWithNodes(3 /* rf */, 3 /* numNodes */, true /* setMasters */);
    JsonNode universeJson = Json.toJson(new UniverseResp(u, null));
    assertThat(
        universeJson.get("universeUUID").asText(),
        allOf(notNullValue(), equalTo(u.getUniverseUUID().toString())));
    JsonNode clusterJson = universeJson.get("universeDetails").get("clusters").get(0);
    assertTrue(clusterJson.get("userIntent").get("regionList").isArray());
    assertNull(clusterJson.get("regions"));
    assertNull(clusterJson.get("provider"));
  }

  @Test
  public void testToJSONOfGFlags() {
    Universe u = createUniverse(defaultCustomer.getId());
    u = Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    UserIntent ui = u.getUniverseDetails().getPrimaryCluster().userIntent;
    ui.provider =
        Provider.get(defaultCustomer.getUuid(), CloudType.aws).get(0).getUuid().toString();
    u.getUniverseDetails().upsertPrimaryCluster(ui, null);

    JsonNode universeJson = Json.toJson(new UniverseResp(u, null));
    assertThat(
        universeJson.get("universeUUID").asText(),
        allOf(notNullValue(), equalTo(u.getUniverseUUID().toString())));
    JsonNode clusterJson = universeJson.get("universeDetails").get("clusters").get(0);
    JsonNode masterGFlags = clusterJson.get("userIntent").get("masterGFlags");
    assertThat(masterGFlags, is(notNullValue()));
    assertEquals(0, masterGFlags.size());
  }

  @Test
  public void testFromJSONWithFlags() {
    Universe u = createUniverse(defaultCustomer.getId());
    Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    UserIntent userIntent = getBaseIntent();
    userIntent.masterGFlags = new HashMap<>();
    userIntent.masterGFlags.put("emulate_redis_responses", "false");
    taskParams.upsertPrimaryCluster(userIntent, null);
    JsonNode clusterJson = Json.toJson(taskParams).get("clusters").get(0);

    assertThat(
        clusterJson.get("userIntent").get("masterGFlags").get("emulate_redis_responses"),
        notNullValue());
  }

  @Test
  public void testAreTagsSame() {
    Universe u = createUniverse(defaultCustomer.getId());
    Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    UserIntent userIntent = getBaseIntent();
    userIntent.providerType = CloudType.aws;
    userIntent.instanceTags = ImmutableMap.of("Cust", "Test", "Dept", "Misc");
    Cluster cluster = taskParams.upsertPrimaryCluster(userIntent, null);

    UserIntent newUserIntent = getBaseIntent();
    newUserIntent.providerType = CloudType.aws;
    newUserIntent.instanceTags = ImmutableMap.of("Cust", "Test", "Dept", "Misc");
    Cluster newCluster = new Cluster(ClusterType.PRIMARY, newUserIntent);
    assertTrue(cluster.areTagsSame(newCluster));

    newUserIntent = getBaseIntent();
    newUserIntent.providerType = CloudType.aws;
    newCluster = new Cluster(ClusterType.PRIMARY, newUserIntent);
    newUserIntent.instanceTags = ImmutableMap.of("Cust", "Test");
    assertFalse(cluster.areTagsSame(newCluster));

    newUserIntent = getBaseIntent();
    newUserIntent.providerType = CloudType.aws;
    newCluster = new Cluster(ClusterType.PRIMARY, newUserIntent);
    assertFalse(cluster.areTagsSame(newCluster));
  }

  // Tags do not apply to non-AWS and GCP provider. This checks that tags check are always
  // considered 'same' for those providers.
  @Test
  public void testAreTagsSameOnAzu() {
    Universe u = createUniverse(defaultCustomer.getId());
    Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    UserIntent userIntent = getBaseIntent();
    userIntent.providerType = CloudType.azu;
    userIntent.instanceTags = ImmutableMap.of("Cust", "Test", "Dept", "Misc");
    Cluster cluster = taskParams.upsertPrimaryCluster(userIntent, null);

    UserIntent newUserIntent = getBaseIntent();
    newUserIntent.providerType = CloudType.azu;
    newUserIntent.instanceTags = ImmutableMap.of("Cust", "Test");
    Cluster newCluster = new Cluster(ClusterType.PRIMARY, newUserIntent);
    assertTrue(cluster.areTagsSame(newCluster));

    newUserIntent = getBaseIntent();
    newUserIntent.providerType = CloudType.azu;
    newCluster = new Cluster(ClusterType.PRIMARY, newUserIntent);
    assertTrue(cluster.areTagsSame(newCluster));
  }

  @Test
  public void testAreTagsSameErrors() {
    Universe u = createUniverse(defaultCustomer.getId());
    Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    UserIntent userIntent = getBaseIntent();
    userIntent.providerType = CloudType.gcp;
    userIntent.instanceTags = ImmutableMap.of("Cust", "Test", "Dept", "Misc");
    Cluster cluster = taskParams.upsertPrimaryCluster(userIntent, null);

    UserIntent newUserIntent = getBaseIntent();
    newUserIntent.providerType = CloudType.aws;
    Cluster newCluster = new Cluster(ClusterType.PRIMARY, newUserIntent);
    newUserIntent.instanceTags = ImmutableMap.of("Cust", "Test");
    try {
      cluster.areTagsSame(newCluster);
    } catch (IllegalArgumentException iae) {
      assertThat(
          iae.getMessage(),
          allOf(notNullValue(), containsString("Mismatched provider " + "types")));
    }
  }

  @Test
  public void testGetUniverses() {
    Config spyConf = spy(app.config());
    doReturn("/tmp/certs").when(spyConf).getString("yb.storage.path");
    UUID certUUID = certificateHelper.createRootCA(spyConf, "test", defaultCustomer.getUuid());
    ModelFactory.createUniverse(defaultCustomer.getId(), certUUID);
    Set<Universe> universes =
        Universe.universeDetailsIfCertsExists(certUUID, defaultCustomer.getUuid());
    assertEquals(universes.size(), 1);

    universes = Universe.universeDetailsIfReleaseExists("");
    assertEquals(universes.size(), 0);
  }

  private UserIntent getBaseIntent() {

    // Create regions
    Region r1 = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    Region r2 = Region.create(defaultProvider, "region-2", "Region 2", "yb-image-1");
    Region r3 = Region.create(defaultProvider, "region-3", "Region 3", "yb-image-1");
    List<UUID> regionList = new ArrayList<>();
    regionList.add(r1.getUuid());
    regionList.add(r2.getUuid());
    regionList.add(r3.getUuid());
    String instanceType = "c3.xlarge";
    // Create userIntent
    UserIntent userIntent = new UserIntent();
    userIntent.replicationFactor = 3;
    userIntent.regionList = regionList;
    userIntent.instanceType = instanceType;
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.storageType = PublicCloudConstants.StorageType.IO1;
    userIntent.deviceInfo.numVolumes = 2;
    userIntent.deviceInfo.diskIops = 1000;
    userIntent.deviceInfo.volumeSize = 100;
    return userIntent;
  }

  @Test
  public void testUpdateNodesDynamicActions_UnderReplicatedMaster_WithoutReadOnlyCluster() {
    // All nodes are created with t-server only. So they are all
    // areMastersUnderReplicated.
    Universe u = createUniverseWithNodes(3 /* rf */, 3 /* numNodes */, false /* setMasters */);

    UniverseResp universeResp = new UniverseResp(u, null);

    JsonNode json = Json.toJson(universeResp).get("universeDetails");

    JsonNode nodeDetailsSet = json.get("nodeDetailsSet");
    assertNotNull(nodeDetailsSet);
    assertTrue(nodeDetailsSet.isArray());
    for (int i = 0; i < nodeDetailsSet.size(); i++) {
      assertTrue(
          jsonArrayHasItem(
              nodeDetailsSet.get(i).get("allowedActions"), NodeActionType.START_MASTER.name()));
    }
  }

  @Test
  // @formatter:off
  @Parameters({
    "host-n4,      true,  true", // underReplicated, node from primary cluster
    "yb-tserver-0, true,  false", // underReplicated, node from read only cluster
    "host-n4,      false, false", // not underReplicated, node from primary cluster
    "yb-tserver-0, false, false" // not underReplicated, node from read only cluster
  })
  // @formatter:on
  public void testUpdateNodesDynamicActions_WithReadOnlyCluster(
      String nodeToTest, boolean isMasterUnderReplicated, boolean expectedResult) {
    Universe u = createUniverse(defaultCustomer.getId());
    UserIntent userIntent = new UserIntent();
    userIntent.replicationFactor = 3;
    userIntent.regionList = new ArrayList<>();
    userIntent.provider =
        Provider.get(defaultCustomer.getUuid(), CloudType.aws).get(0).getUuid().toString();
    userIntent.numNodes = 3;
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithInactiveAndReadReplicaNodes(true, 3));

    if (isMasterUnderReplicated) {
      // Stopping master.
      updateNode(u, "host-n1", NodeState.Stopped, false);
      // One t-server is alive.
      updateNode(u, "host-n4", NodeState.Live, false);
      assertTrue(Util.areMastersUnderReplicated(u.getNode("host-n4"), u));
    }

    UniverseResp universeResp = new UniverseResp(u, null);
    JsonNode json = Json.toJson(universeResp).get("universeDetails");

    JsonNode nodeDetailsSet = json.get("nodeDetailsSet");
    assertNotNull(nodeDetailsSet);
    assertTrue(nodeDetailsSet.isArray());
    assertEquals(
        expectedResult,
        nodeHasAction(nodeDetailsSet, nodeToTest, NodeActionType.START_MASTER.name()));
  }

  // Updates the node state
  private void updateNode(Universe u, String nodeName, NodeState newState, boolean newIsMaster) {
    Universe.UniverseUpdater updater =
        universe -> {
          for (NodeDetails node : u.getUniverseDetails().nodeDetailsSet) {
            if (StringUtils.equals(node.nodeName, nodeName)) {
              node.state = newState;
              node.isMaster = newIsMaster;
              break;
            }
          }
        };
    Universe.saveDetails(u.getUniverseUUID(), updater);
  }

  private static boolean nodeHasAction(
      JsonNode nodeDetailsSet, String nodeName, String actionName) {
    boolean nodeFound = false;
    boolean actionFound = false;
    for (int i = 0; i < nodeDetailsSet.size(); i++) {
      JsonNode jsonNode = nodeDetailsSet.get(i);
      assertNotNull(jsonNode.get("nodeName"));
      if (StringUtils.equals(jsonNode.get("nodeName").asText(), nodeName)) {
        nodeFound = true;
        actionFound = jsonArrayHasItem(jsonNode.get("allowedActions"), actionName);
      }
    }
    assertTrue(nodeFound);
    return actionFound;
  }

  private static boolean jsonArrayHasItem(JsonNode arr, String value) {
    assertNotNull(arr);
    assertTrue(arr.isArray());
    for (JsonNode item : arr) {
      if (StringUtils.equals(item.asText(), value)) {
        return true;
      }
    }
    return false;
  }

  @Test
  public void testGetNodeActions() {
    for (int numNodes = 3; numNodes <= 4; numNodes++) {
      Universe u = createUniverseWithNodes(3 /* rf */, numNodes, true /* setMasters */);
      NodeDetails nd = numNodes == 3 ? u.getNode("host-n1") : u.getNode("host-n4");

      for (NodeDetails.NodeState nodeState : NodeDetails.NodeState.values()) {
        nd.state = nodeState;
        Set<NodeActionType> allowedActions = new AllowedActionsHelper(u, nd).listAllowedActions();

        if (nodeState == NodeDetails.NodeState.ToBeAdded) {
          assertEquals(ImmutableSet.of(NodeActionType.DELETE, NodeActionType.ADD), allowedActions);
        } else if (nodeState == NodeDetails.NodeState.Adding) {
          assertEquals(
              ImmutableSet.of(
                  NodeActionType.DELETE,
                  NodeActionType.RELEASE,
                  NodeActionType.ADD,
                  NodeActionType.REMOVE),
              allowedActions);
        } else if (nodeState == NodeDetails.NodeState.InstanceCreated) {
          assertEquals(ImmutableSet.of(NodeActionType.DELETE, NodeActionType.ADD), allowedActions);
        } else if (nodeState == NodeDetails.NodeState.ServerSetup) {
          assertEquals(ImmutableSet.of(NodeActionType.DELETE, NodeActionType.ADD), allowedActions);
        } else if (nodeState == NodeDetails.NodeState.ToJoinCluster) {
          assertEquals(ImmutableSet.of(NodeActionType.REMOVE, NodeActionType.ADD), allowedActions);
        } else if (nodeState == NodeDetails.NodeState.SoftwareInstalled) {
          assertEquals(
              ImmutableSet.of(NodeActionType.START, NodeActionType.DELETE, NodeActionType.ADD),
              allowedActions);
        } else if (nodeState == NodeDetails.NodeState.ToBeRemoved) {
          assertEquals(ImmutableSet.of(NodeActionType.REMOVE), allowedActions);
        } else if (nodeState == NodeDetails.NodeState.Live) {
          assertEquals(
              ImmutableSet.of(
                  NodeActionType.STOP,
                  NodeActionType.REMOVE,
                  NodeActionType.QUERY,
                  NodeActionType.REBOOT,
                  NodeActionType.HARD_REBOOT,
                  NodeActionType.REPLACE),
              allowedActions);
        } else if (nodeState == NodeDetails.NodeState.Stopped) {
          assertEquals(
              ImmutableSet.of(
                  NodeActionType.START,
                  NodeActionType.REMOVE,
                  NodeActionType.QUERY,
                  NodeActionType.REPROVISION),
              allowedActions);
        } else if (nodeState == NodeDetails.NodeState.Removed) {
          assertEquals(ImmutableSet.of(NodeActionType.ADD, NodeActionType.RELEASE), allowedActions);
        } else if (nodeState == NodeDetails.NodeState.Decommissioned) {
          if (numNodes == 3) {
            // Cannot DELETE node from universe with 3 nodes only - will get only two nodes
            // left.
            assertEquals(ImmutableSet.of(NodeActionType.ADD), allowedActions);
          } else {
            assertEquals(
                ImmutableSet.of(NodeActionType.ADD, NodeActionType.DELETE), allowedActions);
          }
        } else if (nodeState == NodeDetails.NodeState.Provisioned) {
          assertEquals(ImmutableSet.of(NodeActionType.DELETE, NodeActionType.ADD), allowedActions);
        } else if (nodeState == NodeDetails.NodeState.BeingDecommissioned) {
          assertEquals(ImmutableSet.of(NodeActionType.RELEASE), allowedActions);
        } else if (nodeState == NodeDetails.NodeState.Starting) {
          assertEquals(
              ImmutableSet.of(NodeActionType.START, NodeActionType.REMOVE), allowedActions);
        } else if (nodeState == NodeDetails.NodeState.Stopping) {
          assertEquals(ImmutableSet.of(NodeActionType.STOP, NodeActionType.REMOVE), allowedActions);
        } else if (nodeState == NodeDetails.NodeState.Removing) {
          assertEquals(ImmutableSet.of(NodeActionType.REMOVE), allowedActions);
        } else if (nodeState == NodeDetails.NodeState.Terminating) {
          assertEquals(
              ImmutableSet.of(NodeActionType.RELEASE, NodeActionType.DELETE), allowedActions);
        } else if (nodeState == NodeState.Terminated) {
          assertEquals(ImmutableSet.of(NodeActionType.DELETE), allowedActions);
        } else if (nodeState == NodeState.Rebooting) {
          assertEquals(ImmutableSet.of(NodeActionType.REBOOT), allowedActions);
        } else if (nodeState == NodeState.HardRebooting) {
          assertEquals(ImmutableSet.of(NodeActionType.HARD_REBOOT), allowedActions);
        } else {
          assertTrue(allowedActions.isEmpty());
        }
      }
      u.delete();
    }
  }

  private Universe createUniverseWithNodes(int rf, int numNodes, boolean setMasters) {
    return createUniverseWithNodes(rf, numNodes, setMasters, false);
  }

  private Universe createUniverseWithNodes(
      int rf, int numNodes, boolean setMasters, boolean dedicatedNodes) {
    Universe u = createUniverse(defaultCustomer.getId());
    UserIntent userIntent = new UserIntent();
    userIntent.replicationFactor = rf;
    userIntent.regionList = new ArrayList<>();
    userIntent.provider =
        Provider.get(defaultCustomer.getUuid(), CloudType.aws).get(0).getUuid().toString();
    userIntent.numNodes = numNodes;
    userIntent.dedicatedNodes = dedicatedNodes;
    u =
        Universe.saveDetails(
            u.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent, setMasters));
    return u;
  }

  @Test
  public void testGetNodeActions_AllDeletesAllowed() {
    Universe u = createUniverseWithNodes(1 /* rf */, 3 /* numNodes */, true /* setMasters */);
    NodeDetails nd = u.getNodes().iterator().next();

    for (NodeDetails.NodeState nodeState : NodeDetails.NodeState.values()) {
      nd.state = nodeState;
      Set<NodeActionType> actions = new AllowedActionsHelper(u, nd).listAllowedActions();
      assertEquals(nd.isRemovable(), actions.contains(NodeActionType.DELETE));
    }
  }

  @Test
  public void testGetNodeActions_NoStopAndRemoveForOneNodeUniverse() {
    Universe u = createUniverseWithNodes(1 /* rf */, 1 /* numNodes */, true /* setMasters */);
    NodeDetails nd = u.getNodes().iterator().next();
    Set<NodeActionType> actions = new AllowedActionsHelper(u, nd).listAllowedActions();
    assertFalse(actions.contains(NodeActionType.REMOVE));
    assertFalse(actions.contains(NodeActionType.STOP));
  }

  @Test
  public void testGetNodeActions_CheckDeletePresence() {
    Universe u = createUniverseWithNodes(3 /* rf */, 3 /* numNodes */, true /* setMasters */);
    NodeDetails nd = u.getNodes().iterator().next();

    for (NodeDetails.NodeState nodeState : NodeDetails.NodeState.values()) {
      nd.state = nodeState;
      if (!nd.isRemovable()) {
        continue;
      }
      // It is not allowed to remove normal (state == Decommissioned) node from the
      // universe if number of nodes become less than RF.
      Set<NodeActionType> actions = new AllowedActionsHelper(u, nd).listAllowedActions();
      assertEquals(nodeState != NodeState.Decommissioned, actions.contains(NodeActionType.DELETE));
    }
  }

  @Test
  public void testGetNodeActions_NoStartMasterForDedicated() {
    Universe u = createUniverseWithNodes(1 /* rf */, 1 /* numNodes */, true /* setMasters */, true);
    assertEquals(2, u.getNodes().size());
    Map<UniverseTaskBase.ServerType, NodeDetails> nodes =
        u.getNodes().stream().collect(Collectors.toMap(n -> n.dedicatedTo, n -> n));
    NodeDetails masterNode = nodes.get(UniverseTaskBase.ServerType.MASTER);
    NodeDetails tserverNode = nodes.get(UniverseTaskBase.ServerType.TSERVER);
    // temporary disable master and tserver nodes.
    masterNode.isMaster = false;
    tserverNode.isTserver = false;
    Set<NodeActionType> actions = new AllowedActionsHelper(u, tserverNode).listAllowedActions();
    assertFalse(actions.contains(NodeActionType.START_MASTER));
    actions = new AllowedActionsHelper(u, masterNode).listAllowedActions();
    assertFalse(actions.contains(NodeActionType.START_MASTER));
  }

  @Test
  public void testGetNodeActions_StopProcessesForDedicated() {
    Universe u = createUniverseWithNodes(3 /* rf */, 3 /* numNodes */, true /* setMasters */, true);
    assertEquals(6, u.getNodes().size());
    List<NodeDetails> tserverNodes =
        u.getNodes().stream().filter(n -> n.isTserver).collect(Collectors.toList());
    // Temporary disable tserver node.
    tserverNodes.get(0).isTserver = false;
    Set<NodeActionType> actions =
        new AllowedActionsHelper(u, tserverNodes.get(1)).listAllowedActions();
    // Not allowing stopping second tserver (as it will have only one tserver left)
    assertFalse(actions.contains(NodeActionType.STOP));
    assertFalse(actions.contains(NodeActionType.REMOVE));

    // Cannot decommission first tserver node
    actions = new AllowedActionsHelper(u, tserverNodes.get(0)).listAllowedActions();
    assertFalse(actions.contains(NodeActionType.DELETE));
  }

  @Test
  public void testUserIntentOverridesClone() {
    UserIntent intent = new UserIntent();
    intent.regionList = new ArrayList<>();
    intent.universeName = "univ";
    intent.instanceType = "instType";
    intent.provider = "aws";
    intent.ybSoftwareVersion = "1.2.3";
    UserIntent newIntent = intent.clone();
    intent.setUserIntentOverrides(new UniverseDefinitionTaskParams.UserIntentOverrides());
    intent
        .getUserIntentOverrides()
        .setAzOverrides(
            constructOverrides(UUID.randomUUID(), "az1type", UUID.randomUUID(), "az2type"));
    newIntent = intent.clone();
    assertTrue(intent.equals(newIntent));
    assertFalse(newIntent.getUserIntentOverrides() == intent.getUserIntentOverrides());
    assertTrue(Objects.equals(newIntent.getUserIntentOverrides(), intent.getUserIntentOverrides()));
  }

  private Map<UUID, UniverseDefinitionTaskParams.AZOverrides> constructOverrides(
      UUID az1, String type, UUID az2, String type1) {
    UniverseDefinitionTaskParams.AZOverrides azOverrides1 =
        new UniverseDefinitionTaskParams.AZOverrides();
    azOverrides1.setInstanceType(type);
    UniverseDefinitionTaskParams.AZOverrides azOverrides2 =
        new UniverseDefinitionTaskParams.AZOverrides();
    azOverrides2.setInstanceType(type1);
    return ImmutableMap.of(az1, azOverrides1, az2, azOverrides2);
  }

  @Test
  public void testUserIntentOverrides() {
    UniverseDefinitionTaskParams.UserIntentOverrides overrides =
        new UniverseDefinitionTaskParams.UserIntentOverrides();
    UUID az1 = UUID.randomUUID();
    UUID az2 = UUID.randomUUID();
    overrides.setAzOverrides(constructOverrides(az1, "instType1", az2, "instType2"));
    UserIntent userIntent = new UserIntent();
    userIntent.instanceType = "instType";
    userIntent.masterInstanceType = "masterInstType";
    // No overrides
    assertEquals("instType", userIntent.getInstanceType(az1));
    assertEquals("instType", userIntent.getInstanceType(az2));
    assertEquals("instType", userIntent.getInstanceType(UUID.randomUUID()));
    assertEquals("instType", userIntent.getBaseInstanceType());
    assertEquals("instType", userIntent.getInstanceType(UniverseTaskBase.ServerType.TSERVER, null));
    assertEquals("instType", userIntent.getInstanceType(UniverseTaskBase.ServerType.MASTER, null));
    userIntent.dedicatedNodes = true;
    assertEquals("instType", userIntent.getInstanceType(UniverseTaskBase.ServerType.TSERVER, null));
    assertEquals(
        "masterInstType", userIntent.getInstanceType(UniverseTaskBase.ServerType.MASTER, null));
    // With overrides
    userIntent.setUserIntentOverrides(overrides);
    assertEquals("instType1", userIntent.getInstanceType(az1));
    assertEquals("instType2", userIntent.getInstanceType(az2));
    assertEquals("instType", userIntent.getInstanceType(UUID.randomUUID()));
    assertEquals("instType", userIntent.getBaseInstanceType());
    assertEquals("instType1", userIntent.getInstanceType(UniverseTaskBase.ServerType.TSERVER, az1));
    assertEquals(
        "masterInstType", userIntent.getInstanceType(UniverseTaskBase.ServerType.MASTER, az1));
  }
}
