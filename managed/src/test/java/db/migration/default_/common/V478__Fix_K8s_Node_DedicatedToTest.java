// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import io.ebean.DB;
import io.ebean.Transaction;
import java.sql.SQLException;
import java.util.UUID;
import org.junit.Test;
import play.libs.Json;

public class V478__Fix_K8s_Node_DedicatedToTest extends FakeDBApplication {

  @Test
  public void allPodsMaster_tserverFixed() {
    ObjectNode details = details("kubernetes", "MASTER", "MASTER");

    assertTrue(V478__Fix_K8s_Node_DedicatedTo.processUniverse(details, "u"));
    assertEquals("MASTER", dedicatedTo(details, "yb-master-0"));
    assertEquals("TSERVER", dedicatedTo(details, "yb-tserver-0"));
  }

  @Test
  public void allPodsTserver_masterFixed() {
    ObjectNode details = details("kubernetes", "TSERVER", "TSERVER");

    assertTrue(V478__Fix_K8s_Node_DedicatedTo.processUniverse(details, "u"));
    assertEquals("MASTER", dedicatedTo(details, "yb-master-0"));
    assertEquals("TSERVER", dedicatedTo(details, "yb-tserver-0"));
  }

  @Test
  public void allPodsNull_bothSet() {
    ObjectNode details = details("kubernetes", null, null);

    assertTrue(V478__Fix_K8s_Node_DedicatedTo.processUniverse(details, "u"));
    assertEquals("MASTER", dedicatedTo(details, "yb-master-0"));
    assertEquals("TSERVER", dedicatedTo(details, "yb-tserver-0"));
  }

  @Test
  public void consistent_noUpdate() {
    ObjectNode details = details("kubernetes", "MASTER", "TSERVER");
    ObjectNode before = details.deepCopy();

    assertFalse(V478__Fix_K8s_Node_DedicatedTo.processUniverse(details, "u"));
    assertEquals(before, details);
  }

  @Test
  public void nonK8s_noUpdate() {
    ObjectNode details = details("aws", "MASTER", "MASTER");
    ObjectNode before = details.deepCopy();

    assertFalse(V478__Fix_K8s_Node_DedicatedTo.processUniverse(details, "u"));
    assertEquals(before, details);
  }

  @Test
  public void nonDedicatedK8s_noUpdate() {
    ObjectNode details = details("kubernetes", null, "MASTER");
    ((ObjectNode) details.get("clusters").get(0).get("userIntent")).put("dedicatedNodes", false);
    ObjectNode before = details.deepCopy();

    assertFalse(V478__Fix_K8s_Node_DedicatedTo.processUniverse(details, "u"));
    assertEquals(before, details);
  }

  @Test
  public void migrate_tserverPodGetsTserverInstanceType() throws SQLException {
    UUID universeUUID = createK8sUniverse("v478-master", ServerType.MASTER);
    assertEquals("xsmall", instanceTypeForNode(universeUUID, "yb-tserver-0"));

    runMigration();

    assertEquals("small", instanceTypeForNode(universeUUID, "yb-tserver-0"));
    assertEquals("xsmall", instanceTypeForNode(universeUUID, "yb-master-0"));
  }

  @Test
  public void migrate_nullMasterPodGetsMasterInstanceType() throws SQLException {
    UUID universeUUID = createK8sUniverse("v478-null", null);
    assertEquals("small", instanceTypeForNode(universeUUID, "yb-master-0"));

    runMigration();

    assertEquals("xsmall", instanceTypeForNode(universeUUID, "yb-master-0"));
    assertEquals("small", instanceTypeForNode(universeUUID, "yb-tserver-0"));
  }

  // Every pod gets podsDedicatedTo, as pre-2025.1 POD_INFO left them after cloning one node.
  private static UUID createK8sUniverse(String name, ServerType podsDedicatedTo) {
    Customer customer = ModelFactory.testCustomer();
    UUID universeUUID =
        ModelFactory.createUniverse(name, customer.getId(), CloudType.kubernetes).getUniverseUUID();
    Universe.saveDetails(
        universeUUID,
        u -> {
          UserIntent intent = u.getUniverseDetails().getPrimaryCluster().userIntent;
          intent.dedicatedNodes = true;
          intent.instanceType = "small";
          intent.masterInstanceType = "xsmall";
          UUID clusterUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
          u.getUniverseDetails()
              .nodeDetailsSet
              .add(pod("yb-master-0", clusterUuid, true, podsDedicatedTo));
          u.getUniverseDetails()
              .nodeDetailsSet
              .add(pod("yb-tserver-0", clusterUuid, false, podsDedicatedTo));
        });
    return universeUUID;
  }

  private static void runMigration() throws SQLException {
    Transaction transaction = DB.beginTransaction();
    try {
      V478__Fix_K8s_Node_DedicatedTo.migrate(transaction.connection());
      DB.commitTransaction();
    } finally {
      DB.endTransaction();
    }
  }

  private static String instanceTypeForNode(UUID universeUUID, String nodeName) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    return universe
        .getUniverseDetails()
        .getPrimaryCluster()
        .userIntent
        .getInstanceTypeForNode(universe.getNode(nodeName));
  }

  private static NodeDetails pod(
      String nodeName, UUID placementUuid, boolean isMaster, ServerType dedicatedTo) {
    NodeDetails node = new NodeDetails();
    node.nodeName = nodeName;
    node.placementUuid = placementUuid;
    node.isMaster = isMaster;
    node.isTserver = !isMaster;
    node.dedicatedTo = dedicatedTo;
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.instance_type = "xsmall";
    return node;
  }

  private static ObjectNode details(
      String providerType, String masterDedicatedTo, String tserverDedicatedTo) {
    String clusterUuid = UUID.randomUUID().toString();
    ObjectNode details = Json.newObject();
    ObjectNode cluster = details.putArray("clusters").addObject();
    cluster.put("uuid", clusterUuid);
    cluster.put("clusterType", "PRIMARY");
    cluster.putObject("userIntent").put("providerType", providerType).put("dedicatedNodes", true);
    ArrayNode nodes = details.putArray("nodeDetailsSet");
    addNode(nodes, "yb-master-0", clusterUuid, true, masterDedicatedTo);
    addNode(nodes, "yb-tserver-0", clusterUuid, false, tserverDedicatedTo);
    return details;
  }

  private static void addNode(
      ArrayNode nodes,
      String nodeName,
      String placementUuid,
      boolean isMaster,
      String dedicatedTo) {
    ObjectNode node = nodes.addObject();
    node.put("nodeName", nodeName);
    node.put("placementUuid", placementUuid);
    node.put("isMaster", isMaster);
    node.put("isTserver", !isMaster);
    node.put("dedicatedTo", dedicatedTo);
  }

  private static String dedicatedTo(JsonNode details, String nodeName) {
    for (JsonNode node : details.get("nodeDetailsSet")) {
      if (nodeName.equals(node.get("nodeName").asText())) {
        return node.get("dedicatedTo").asText(null);
      }
    }
    throw new AssertionError("No node " + nodeName);
  }
}
