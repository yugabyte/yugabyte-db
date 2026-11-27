// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
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
  public void vmPrimary_cleared(String providerType) {
    ObjectNode details = details("PRIMARY", true);
    userIntent(details).put("providerType", providerType);

    assertTrue(V482__Clear_Ybdb_Inbuilt_Ybc_On_Non_K8s.processUniverse(details, "u"));
    assertFalse(userIntent(details).get("useYbdbInbuiltYbc").asBoolean());
  }

  @Test
  public void multicloudVmPrimary_cleared() {
    ObjectNode details = details("PRIMARY", true);
    setProviderSpecs(details, "aws", "gcp");

    assertTrue(V482__Clear_Ybdb_Inbuilt_Ybc_On_Non_K8s.processUniverse(details, "u"));
    assertFalse(userIntent(details).get("useYbdbInbuiltYbc").asBoolean());
  }

  @Test
  @Parameters({"kubernetes", "unknown"})
  public void k8sOrUnknownPrimary_noUpdate(String providerType) {
    ObjectNode details = details("PRIMARY", true);
    userIntent(details).put("providerType", providerType);
    assertNoUpdate(details);
  }

  @Test
  public void missingProviderType_noUpdate() {
    assertNoUpdate(details("PRIMARY", true));
  }

  @Test
  public void multicloudWithK8sSpec_noUpdate() {
    ObjectNode details = details("PRIMARY", true);
    setProviderSpecs(details, "aws", "kubernetes");
    assertNoUpdate(details);
  }

  @Test
  public void vmPrimaryWithoutFlag_noUpdate() {
    ObjectNode details = details("PRIMARY", false);
    userIntent(details).put("providerType", "aws");
    assertNoUpdate(details);
  }

  // The flag is read only from the primary cluster.
  @Test
  public void vmReadReplica_noUpdate() {
    ObjectNode details = details("ASYNC", true);
    userIntent(details).put("providerType", "aws");
    assertNoUpdate(details);
  }

  @Test
  public void migrate_clearsVmAndKeepsK8s() throws SQLException {
    Customer customer = ModelFactory.testCustomer();
    UUID vmUniverse = createUniverseWithInbuiltYbc(customer, "v482-vm", CloudType.aws);
    UUID k8sUniverse = createUniverseWithInbuiltYbc(customer, "v482-k8s", CloudType.kubernetes);

    Transaction transaction = DB.beginTransaction();
    try {
      V482__Clear_Ybdb_Inbuilt_Ybc_On_Non_K8s.migrate(transaction.connection());
      DB.commitTransaction();
    } finally {
      DB.endTransaction();
    }

    assertFalse(usesInbuiltYbc(vmUniverse));
    assertTrue(usesInbuiltYbc(k8sUniverse));
  }

  private static UUID createUniverseWithInbuiltYbc(
      Customer customer, String name, CloudType cloudType) {
    UUID universeUUID =
        ModelFactory.createUniverse(name, customer.getId(), cloudType).getUniverseUUID();
    Universe.saveDetails(
        universeUUID,
        u -> u.getUniverseDetails().getPrimaryCluster().userIntent.setUseYbdbInbuiltYbc(true));
    return universeUUID;
  }

  private static boolean usesInbuiltYbc(UUID universeUUID) {
    return Universe.getOrBadRequest(universeUUID)
        .getUniverseDetails()
        .getPrimaryCluster()
        .userIntent
        .isUseYbdbInbuiltYbc();
  }

  private static void assertNoUpdate(ObjectNode details) {
    ObjectNode before = details.deepCopy();
    assertFalse(V482__Clear_Ybdb_Inbuilt_Ybc_On_Non_K8s.processUniverse(details, "u"));
    assertEquals(before, details);
  }

  private static ObjectNode details(String clusterType, boolean useYbdbInbuiltYbc) {
    ObjectNode details = Json.newObject();
    ObjectNode cluster = details.putArray("clusters").addObject();
    cluster.put("uuid", UUID.randomUUID().toString());
    cluster.put("clusterType", clusterType);
    cluster.putObject("userIntent").put("useYbdbInbuiltYbc", useYbdbInbuiltYbc);
    return details;
  }

  private static void setProviderSpecs(ObjectNode details, String... providerTypes) {
    ArrayNode specs = userIntent(details).putArray("providerSpecifications");
    for (String providerType : providerTypes) {
      specs.addObject().put("providerType", providerType);
    }
  }

  private static ObjectNode userIntent(ObjectNode details) {
    return (ObjectNode) details.get("clusters").get(0).get("userIntent");
  }
}
