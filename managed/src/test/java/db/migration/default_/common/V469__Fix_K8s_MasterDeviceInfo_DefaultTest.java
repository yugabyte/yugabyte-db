// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
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

public class V469__Fix_K8s_MasterDeviceInfo_DefaultTest extends FakeDBApplication {

  @Test
  public void k8sV459Default_replacedWithDeviceInfo() throws Exception {
    ObjectNode details = k8sDetailsWithNullMaster(new DeviceInfo(2, 200));
    ((ObjectNode) details.get("clusters").get(0).get("userIntent").get("deviceInfo"))
        .put("storageClass", "yb-standard");

    assertTrue(V459__Init_MasterInfo_For_Dedicated.processUniverse(details, "u-k8s"));
    assertTrue(V469__Fix_K8s_MasterDeviceInfo_Default.processUniverse(details, "u-k8s"));

    UserIntent userIntent = primaryUserIntent(details);
    assertEquals(Integer.valueOf(200), userIntent.masterDeviceInfo.volumeSize);
    assertEquals(Integer.valueOf(2), userIntent.masterDeviceInfo.numVolumes);
    assertEquals(Integer.valueOf(200), userIntent.deviceInfo.volumeSize);
    assertEquals(Integer.valueOf(2), userIntent.deviceInfo.numVolumes);
    assertEquals(
        "yb-standard",
        details
            .get("clusters")
            .get(0)
            .get("userIntent")
            .get("masterDeviceInfo")
            .get("storageClass")
            .asText());
  }

  @Test
  public void k8sPersistedDefault_noUpdate() throws Exception {
    ObjectNode details = k8sDetailsWithNullMaster(new DeviceInfo(2, 200));
    ObjectNode userIntent = (ObjectNode) details.get("clusters").get(0).get("userIntent");
    ObjectNode masterDeviceInfo = Json.newObject();
    masterDeviceInfo.put("volumeSize", 50);
    masterDeviceInfo.put("numVolumes", 1);
    masterDeviceInfo.put("storageClass", "");
    userIntent.set("masterDeviceInfo", masterDeviceInfo);
    ObjectNode before = details.deepCopy();

    assertFalse(V469__Fix_K8s_MasterDeviceInfo_Default.processUniverse(details, "u-real"));
    assertEquals(before, details);
  }

  @Test
  public void nonK8sV459Shape_noUpdate() throws Exception {
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    UserIntent userIntent = new UserIntent();
    userIntent.dedicatedNodes = true;
    userIntent.providerType = CloudType.aws;
    userIntent.deviceInfo = new DeviceInfo(2, 100);
    params.clusters.add(new Cluster(ClusterType.PRIMARY, userIntent));
    ObjectNode details = (ObjectNode) Json.toJson(params);
    ((ObjectNode) details.get("clusters").get(0).get("userIntent"))
        .set("masterDeviceInfo", Json.parse("{\"volumeSize\": 50, \"numVolumes\": 1}"));
    ObjectNode before = details.deepCopy();

    assertFalse(V469__Fix_K8s_MasterDeviceInfo_Default.processUniverse(details, "u-aws"));
    assertEquals(before, details);
  }

  @Test
  public void migrateReplacesV459DefaultAndKeepsRealDefault() throws SQLException {
    Customer customer = ModelFactory.testCustomer();
    UUID v459Universe = kubernetesUniverse(customer, "v469-v459", /* v459Default */ true);
    UUID realDefaultUniverse = kubernetesUniverse(customer, "v469-real", /* v459Default */ false);

    Transaction transaction = DB.beginTransaction();
    try {
      V459__Init_MasterInfo_For_Dedicated.migrate(transaction.connection());
      V469__Fix_K8s_MasterDeviceInfo_Default.migrate(transaction.connection());
      DB.commitTransaction();
    } finally {
      DB.endTransaction();
    }

    com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent fixed =
        Universe.getOrBadRequest(v459Universe).getUniverseDetails().getPrimaryCluster().userIntent;
    assertEquals(Integer.valueOf(200), fixed.masterDeviceInfo.volumeSize);
    assertEquals(Integer.valueOf(2), fixed.masterDeviceInfo.numVolumes);
    assertEquals("yb-standard", fixed.masterDeviceInfo.storageClass);

    com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent untouched =
        Universe.getOrBadRequest(realDefaultUniverse)
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent;
    assertEquals(Integer.valueOf(50), untouched.masterDeviceInfo.volumeSize);
    assertEquals(Integer.valueOf(1), untouched.masterDeviceInfo.numVolumes);
  }

  private static UUID kubernetesUniverse(Customer customer, String name, boolean v459Default) {
    Universe universe = ModelFactory.createUniverse(name, customer.getId(), CloudType.kubernetes);
    UUID universeUUID = universe.getUniverseUUID();
    Universe.saveDetails(
        universeUUID,
        u -> {
          com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent intent =
              u.getUniverseDetails().getPrimaryCluster().userIntent;
          intent.dedicatedNodes = true;
          intent.providerType = CloudType.kubernetes;
          intent.deviceInfo = ApiUtils.getDummyDeviceInfo(2, 200);
          intent.deviceInfo.storageClass = "yb-standard";
          if (v459Default) {
            intent.masterDeviceInfo = null;
          } else {
            intent.masterDeviceInfo = OperatorUtils.defaultMasterDeviceInfo();
          }
        });
    return universeUUID;
  }

  private static ObjectNode k8sDetailsWithNullMaster(DeviceInfo deviceInfo) {
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    UserIntent userIntent = new UserIntent();
    userIntent.dedicatedNodes = true;
    userIntent.providerType = CloudType.kubernetes;
    userIntent.instanceType = "c5.large";
    userIntent.deviceInfo = deviceInfo;
    params.clusters.add(new Cluster(ClusterType.PRIMARY, userIntent));
    return (ObjectNode) Json.toJson(params);
  }

  private static UserIntent primaryUserIntent(ObjectNode details) {
    return Json.fromJson(details, UniverseDefinitionTaskParams.class).clusters.get(0).userIntent;
  }
}
