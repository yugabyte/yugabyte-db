// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.migrations.V459.Cluster;
import com.yugabyte.yw.models.migrations.V459.ClusterType;
import com.yugabyte.yw.models.migrations.V459.DeviceInfo;
import com.yugabyte.yw.models.migrations.V459.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.migrations.V459.UserIntent;
import io.ebean.DB;
import io.ebean.Transaction;
import java.sql.SQLException;
import java.util.UUID;
import org.junit.Test;
import play.libs.Json;

public class V459__Init_MasterInfo_For_DedicatedTest extends FakeDBApplication {

  @Test
  public void awsDedicatedMissingMasterFields_clonesDeviceInfoAndInstanceType() throws Exception {
    UniverseDefinitionTaskParams params =
        buildUniverseDetails(
            ClusterType.PRIMARY,
            CloudType.aws,
            true /* dedicated */,
            "c5.large",
            new DeviceInfo(2, 100),
            null /* masterDeviceInfo */,
            null /* masterInstanceType */,
            false /* includeReadReplica */);
    ObjectNode details = toDetailsJson(params);

    assertTrue(V459__Init_MasterInfo_For_Dedicated.processUniverse(details, "u-aws"));

    UserIntent userIntent = primaryUserIntent(details);
    assertEquals(Integer.valueOf(100), userIntent.masterDeviceInfo.volumeSize);
    assertEquals(Integer.valueOf(2), userIntent.masterDeviceInfo.numVolumes);
    assertEquals("c5.large", userIntent.masterInstanceType);
    // Original deviceInfo must be unchanged (deep copy).
    assertEquals(Integer.valueOf(100), userIntent.deviceInfo.volumeSize);
  }

  @Test
  public void k8sDedicatedMissingMasterFields_usesDefaultMasterDeviceInfo() throws Exception {
    UniverseDefinitionTaskParams params =
        buildUniverseDetails(
            ClusterType.PRIMARY,
            CloudType.kubernetes,
            true,
            "c5.large",
            new DeviceInfo(1, 200),
            null,
            null,
            false);
    ObjectNode details = toDetailsJson(params);

    assertTrue(V459__Init_MasterInfo_For_Dedicated.processUniverse(details, "u-k8s"));

    UserIntent userIntent = primaryUserIntent(details);
    assertEquals(Integer.valueOf(50), userIntent.masterDeviceInfo.volumeSize);
    assertEquals(Integer.valueOf(1), userIntent.masterDeviceInfo.numVolumes);
    assertEquals("c5.large", userIntent.masterInstanceType);
  }

  @Test
  public void alreadyPopulatedMasterFields_noUpdate() throws Exception {
    UniverseDefinitionTaskParams params =
        buildUniverseDetails(
            ClusterType.PRIMARY,
            CloudType.aws,
            true,
            "c5.large",
            new DeviceInfo(1, 100),
            new DeviceInfo(1, 50),
            "m5.large",
            false);
    ObjectNode details = toDetailsJson(params);
    ObjectNode before = details.deepCopy();

    assertFalse(V459__Init_MasterInfo_For_Dedicated.processUniverse(details, "u-done"));
    assertEquals(before, details);
  }

  @Test
  public void notDedicated_noUpdate() throws Exception {
    UniverseDefinitionTaskParams params =
        buildUniverseDetails(
            ClusterType.PRIMARY,
            CloudType.aws,
            false,
            "c5.large",
            new DeviceInfo(1, 100),
            null,
            null,
            false);
    ObjectNode details = toDetailsJson(params);

    assertFalse(V459__Init_MasterInfo_For_Dedicated.processUniverse(details, "u-shared"));
    UserIntent userIntent = primaryUserIntent(details);
    assertNull(userIntent.masterDeviceInfo);
    assertNull(userIntent.masterInstanceType);
  }

  @Test
  public void jsonNullMasterFields_areBackfilled() throws Exception {
    UniverseDefinitionTaskParams params =
        buildUniverseDetails(
            ClusterType.PRIMARY,
            CloudType.aws,
            true,
            "c5.xlarge",
            new DeviceInfo(1, 80),
            null,
            null,
            false);
    ObjectNode details = toDetailsJson(params);
    ObjectNode userIntentNode = (ObjectNode) details.get("clusters").get(0).get("userIntent");
    userIntentNode.putNull("masterDeviceInfo");
    userIntentNode.putNull("masterInstanceType");

    assertTrue(V459__Init_MasterInfo_For_Dedicated.processUniverse(details, "u-null"));

    UserIntent userIntent = primaryUserIntent(details);
    assertNotNull(userIntent.masterDeviceInfo);
    assertEquals(Integer.valueOf(80), userIntent.masterDeviceInfo.volumeSize);
    assertEquals("c5.xlarge", userIntent.masterInstanceType);
  }

  @Test
  public void missingInstanceType_stillBackfillsMasterDeviceInfo() throws Exception {
    UniverseDefinitionTaskParams params =
        buildUniverseDetails(
            ClusterType.PRIMARY,
            CloudType.kubernetes,
            true,
            null,
            new DeviceInfo(1, 100),
            null,
            null,
            false);
    ObjectNode details = toDetailsJson(params);

    assertTrue(V459__Init_MasterInfo_For_Dedicated.processUniverse(details, "u-no-itype"));

    UserIntent userIntent = primaryUserIntent(details);
    assertEquals(Integer.valueOf(50), userIntent.masterDeviceInfo.volumeSize);
    assertNull(userIntent.masterInstanceType);
  }

  @Test
  public void readReplicaPresent_stillUpdatesPrimary() throws Exception {
    UniverseDefinitionTaskParams params =
        buildUniverseDetails(
            ClusterType.PRIMARY,
            CloudType.aws,
            true,
            "c5.large",
            new DeviceInfo(1, 100),
            null,
            null,
            true);
    ObjectNode details = toDetailsJson(params);

    assertTrue(V459__Init_MasterInfo_For_Dedicated.processUniverse(details, "u-rr"));

    UniverseDefinitionTaskParams updated =
        Json.fromJson(details, UniverseDefinitionTaskParams.class);
    UserIntent primaryIntent = updated.clusters.get(0).userIntent;
    assertNotNull(primaryIntent.masterDeviceInfo);
    assertEquals("c5.large", primaryIntent.masterInstanceType);

    UserIntent rrIntent = updated.clusters.get(1).userIntent;
    assertNull(rrIntent.masterDeviceInfo);
    assertNull(rrIntent.masterInstanceType);
  }

  @Test
  public void missingDeviceInfo_noUpdate() throws Exception {
    UniverseDefinitionTaskParams params =
        buildUniverseDetails(
            ClusterType.PRIMARY, CloudType.aws, true, "c5.large", null, null, null, false);
    ObjectNode details = toDetailsJson(params);

    assertFalse(V459__Init_MasterInfo_For_Dedicated.processUniverse(details, "u-no-di"));
  }

  @Test
  public void migratePersistsUpdates() throws SQLException {
    Customer customer = ModelFactory.testCustomer();
    Universe universe = ModelFactory.createUniverse("v459-univ", customer.getId());
    UUID universeUUID = universe.getUniverseUUID();

    Universe.saveDetails(
        universeUUID,
        u -> {
          com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent intent =
              u.getUniverseDetails().getPrimaryCluster().userIntent;
          intent.dedicatedNodes = true;
          intent.instanceType = "c5.large";
          intent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
          intent.masterDeviceInfo = null;
          intent.masterInstanceType = null;
        });

    Transaction transaction = DB.beginTransaction();
    try {
      V459__Init_MasterInfo_For_Dedicated.migrate(transaction.connection());
      DB.commitTransaction();
    } finally {
      DB.endTransaction();
    }

    universe = Universe.getOrBadRequest(universeUUID);
    com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent intent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    assertNotNull(intent.masterDeviceInfo);
    assertEquals(Integer.valueOf(100), intent.masterDeviceInfo.volumeSize);
    assertEquals(Integer.valueOf(1), intent.masterDeviceInfo.numVolumes);
    assertEquals("c5.large", intent.masterInstanceType);
  }

  @Test
  public void migrateSkipsNonDedicatedUniverses() throws SQLException {
    Customer customer = ModelFactory.testCustomer();
    Universe universe = ModelFactory.createUniverse("v459-shared", customer.getId());
    UUID universeUUID = universe.getUniverseUUID();

    Universe.saveDetails(
        universeUUID,
        u -> {
          com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent intent =
              u.getUniverseDetails().getPrimaryCluster().userIntent;
          intent.dedicatedNodes = false;
          intent.masterDeviceInfo = null;
          intent.masterInstanceType = null;
        });

    Transaction transaction = DB.beginTransaction();
    try {
      V459__Init_MasterInfo_For_Dedicated.migrate(transaction.connection());
      DB.commitTransaction();
    } finally {
      DB.endTransaction();
    }

    universe = Universe.getOrBadRequest(universeUUID);
    com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent intent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    assertNull(intent.masterDeviceInfo);
    assertNull(intent.masterInstanceType);
  }

  private static ObjectNode toDetailsJson(UniverseDefinitionTaskParams params) {
    JsonNode node = Json.toJson(params);
    assertTrue(node instanceof ObjectNode);
    return (ObjectNode) node;
  }

  private static UserIntent primaryUserIntent(ObjectNode details) {
    return Json.fromJson(details, UniverseDefinitionTaskParams.class).clusters.get(0).userIntent;
  }

  private static UniverseDefinitionTaskParams buildUniverseDetails(
      ClusterType clusterType,
      CloudType providerType,
      boolean dedicatedNodes,
      String instanceType,
      DeviceInfo deviceInfo,
      DeviceInfo masterDeviceInfo,
      String masterInstanceType,
      boolean includeReadReplica) {
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.clusters.add(
        cluster(
            clusterType,
            providerType,
            dedicatedNodes,
            instanceType,
            deviceInfo,
            masterDeviceInfo,
            masterInstanceType));
    if (includeReadReplica) {
      DeviceInfo rrDeviceInfo =
          deviceInfo == null ? null : new DeviceInfo(deviceInfo.numVolumes, deviceInfo.volumeSize);
      params.clusters.add(
          cluster(ClusterType.ASYNC, providerType, false, instanceType, rrDeviceInfo, null, null));
    }
    return params;
  }

  private static Cluster cluster(
      ClusterType clusterType,
      CloudType providerType,
      boolean dedicatedNodes,
      String instanceType,
      DeviceInfo deviceInfo,
      DeviceInfo masterDeviceInfo,
      String masterInstanceType) {
    UserIntent userIntent = new UserIntent();
    userIntent.dedicatedNodes = dedicatedNodes;
    userIntent.providerType = providerType;
    userIntent.instanceType = instanceType;
    userIntent.deviceInfo = deviceInfo;
    userIntent.masterDeviceInfo = masterDeviceInfo;
    userIntent.masterInstanceType = masterInstanceType;
    return new Cluster(clusterType, userIntent);
  }
}
