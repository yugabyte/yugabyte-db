// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.BooleanNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.DeltaEvaluator;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeDetailsArrayComparator;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.StateTransitionDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.DB;
import io.ebean.Transaction;
import java.sql.SQLException;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class V482__Clear_Ybdb_Inbuilt_Ybc_On_Non_K8sTest extends FakeDBApplication {

  @Test
  @Parameters({"aws", "gcp", "azu", "onprem"})
  public void vmPrimary_known(String providerType) {
    ObjectNode details = details();
    primaryIntent(details).put("providerType", providerType);
    assertTrue(V482__Clear_Ybdb_Inbuilt_Ybc_On_Non_K8s.isKnownNonK8sUniverse(details));
  }

  @Test
  public void multicloudVmPrimary_known() {
    ObjectNode details = details();
    setProviderSpecs(details, "aws", "gcp");
    assertTrue(V482__Clear_Ybdb_Inbuilt_Ybc_On_Non_K8s.isKnownNonK8sUniverse(details));
  }

  @Test
  @Parameters({"kubernetes", "unknown"})
  public void k8sOrUnknownPrimary_notKnown(String providerType) {
    ObjectNode details = details();
    primaryIntent(details).put("providerType", providerType);
    assertFalse(V482__Clear_Ybdb_Inbuilt_Ybc_On_Non_K8s.isKnownNonK8sUniverse(details));
  }

  @Test
  public void missingProviderType_notKnown() {
    assertFalse(V482__Clear_Ybdb_Inbuilt_Ybc_On_Non_K8s.isKnownNonK8sUniverse(details()));
  }

  @Test
  public void multicloudWithK8sSpec_notKnown() {
    ObjectNode details = details();
    setProviderSpecs(details, "aws", "kubernetes");
    assertFalse(V482__Clear_Ybdb_Inbuilt_Ybc_On_Non_K8s.isKnownNonK8sUniverse(details));
  }

  // A checkpoint keeps an unchanged value as is and a changed one as a delta node.
  @Test
  public void clearFlag_clearsPlainValuesAndDeltaNodes() {
    ObjectNode details = details();
    primaryIntent(details).put("useYbdbInbuiltYbc", true);
    ObjectNode readReplica = ((ArrayNode) details.get("clusters")).addObject();
    readReplica.put("clusterType", "ASYNC");
    readReplica
        .putObject("userIntent")
        .set(
            "useYbdbInbuiltYbc",
            Json.parse("{\"$oldValue\":false,\"$newValue\":true,\"$deltaType\":\"REPLACE\"}"));

    assertTrue(V482__Clear_Ybdb_Inbuilt_Ybc_On_Non_K8s.clearFlag(details));
    assertEquals(BooleanNode.FALSE, details.at("/clusters/0/userIntent/useYbdbInbuiltYbc"));
    assertEquals(BooleanNode.FALSE, details.at("/clusters/1/userIntent/useYbdbInbuiltYbc"));
  }

  @Test
  public void clearFlag_alreadyFalse_noUpdate() {
    ObjectNode details = details();
    primaryIntent(details).put("useYbdbInbuiltYbc", false);
    ObjectNode before = details.deepCopy();

    assertFalse(V482__Clear_Ybdb_Inbuilt_Ybc_On_Non_K8s.clearFlag(details));
    assertEquals(before, details);
  }

  @Test
  public void migrate_clearsEveryVmCopyAndKeepsK8s() throws SQLException {
    Customer customer = ModelFactory.testCustomer();
    UUID vmUniverse = createUniverseWithInbuiltYbc(customer, "v482-vm", CloudType.aws);
    UUID k8sUniverse = createUniverseWithInbuiltYbc(customer, "v482-k8s", CloudType.kubernetes);
    UUID unownedTask =
        saveTask(Json.toJson(Universe.getOrBadRequest(vmUniverse).getUniverseDetails()));

    Transaction transaction = DB.beginTransaction();
    try {
      V482__Clear_Ybdb_Inbuilt_Ybc_On_Non_K8s.migrate(transaction.connection());
      DB.commitTransaction();
    } finally {
      DB.endTransaction();
    }

    assertCopies(vmUniverse, false);
    assertCopies(k8sUniverse, true);
    assertTrue(usesInbuiltYbc(TaskInfo.getOrBadRequest(unownedTask).getTaskParams()));
  }

  // Leaves the universe as a failed edit does: owned by the edit task and with its checkpoint.
  private static UUID createUniverseWithInbuiltYbc(
      Customer customer, String name, CloudType cloudType) {
    UUID universeUUID =
        ModelFactory.createUniverse(name, customer.getId(), cloudType).getUniverseUUID();
    Universe.saveDetails(
        universeUUID,
        u -> u.getUniverseDetails().getPrimaryCluster().userIntent.setUseYbdbInbuiltYbc(true));
    UniverseDefinitionTaskParams before =
        Universe.getOrBadRequest(universeUUID).getUniverseDetails();
    UniverseDefinitionTaskParams target =
        Json.fromJson(Json.toJson(before), UniverseDefinitionTaskParams.class);
    target.getPrimaryCluster().userIntent.numNodes++;
    JsonNode delta =
        DeltaEvaluator.buildDeltaJsonTree(before, target, new NodeDetailsArrayComparator());
    UUID editTask = saveTask(Json.toJson(target));
    Universe.saveDetails(
        universeUUID,
        u -> {
          u.getUniverseDetails().placementModificationTaskUuid = editTask;
          u.setStateTransitionDetails(new StateTransitionDetails(true, delta));
        });
    return universeUUID;
  }

  private static UUID saveTask(JsonNode params) {
    TaskInfo task = new TaskInfo(TaskType.EditUniverse, null);
    task.setOwner("test");
    task.setTaskState(TaskInfo.State.Failure);
    task.setTaskParams(params);
    task.save();
    return task.getUuid();
  }

  private static void assertCopies(UUID universeUUID, boolean expected) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    StateTransitionDetails checkpoint = universe.getStateTransitionDetails();
    assertEquals(expected, details.getPrimaryCluster().userIntent.isUseYbdbInbuiltYbc());
    assertEquals(
        expected,
        checkpoint.getBeforeUniverseDetails().getPrimaryCluster().userIntent.isUseYbdbInbuiltYbc());
    assertEquals(
        expected,
        checkpoint.getTargetUniverseDetails().getPrimaryCluster().userIntent.isUseYbdbInbuiltYbc());
    TaskInfo editTask = TaskInfo.getOrBadRequest(details.placementModificationTaskUuid);
    assertEquals(expected, usesInbuiltYbc(editTask.getTaskParams()));
  }

  private static boolean usesInbuiltYbc(JsonNode taskParams) {
    return Json.fromJson(taskParams, UniverseDefinitionTaskParams.class)
        .getPrimaryCluster()
        .userIntent
        .isUseYbdbInbuiltYbc();
  }

  private static ObjectNode details() {
    ObjectNode details = Json.newObject();
    ObjectNode cluster = details.putArray("clusters").addObject();
    cluster.put("uuid", UUID.randomUUID().toString());
    cluster.put("clusterType", "PRIMARY");
    cluster.putObject("userIntent");
    return details;
  }

  private static void setProviderSpecs(ObjectNode details, String... providerTypes) {
    ArrayNode specs = primaryIntent(details).putArray("providerSpecifications");
    for (String providerType : providerTypes) {
      specs.addObject().put("providerType", providerType);
    }
  }

  private static ObjectNode primaryIntent(ObjectNode details) {
    return (ObjectNode) details.get("clusters").get(0).get("userIntent");
  }
}
