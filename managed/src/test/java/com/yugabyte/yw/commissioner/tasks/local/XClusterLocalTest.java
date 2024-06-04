// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.Util.YUGABYTE_DB;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.LocalNodeManager;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.TableInfoForm;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashSet;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public class XClusterLocalTest extends LocalProviderUniverseTestBase {

  @Override
  protected Pair<Integer, Integer> getIpRange() {
    return new Pair<>(120, 150);
  }

  private Result createXClusterConfig(XClusterConfigCreateFormData formData) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        app,
        "POST",
        "/api/customers/" + customer.getUuid() + "/xcluster_configs",
        user.createAuthToken(),
        Json.toJson(formData));
  }

  private Result editXClusterConfig(XClusterConfigEditFormData formData, UUID xClusterUUID) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        app,
        "PUT",
        "/api/customers/" + customer.getUuid() + "/xcluster_configs/" + xClusterUUID,
        user.createAuthToken(),
        Json.toJson(formData));
  }

  @Test
  public void testXClusterConfigSetup() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getDefaultUserIntent("source-universe", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe source = createUniverseWithYbc(userIntent);
    initYSQL(source);
    initAndStartPayload(source);

    userIntent = getDefaultUserIntent("target-universe", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe target = createUniverseWithYbc(userIntent);

    // Set up the storage config.
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", baseDir);
    log.info("Customer config here: {}", customerConfig.toString());

    // Get the table info for the source universe.
    List<TableInfoForm.TableInfoResp> resp =
        tableHandler.listTables(
            customer.getUuid(), source.getUniverseUUID(), false, false, true, true);

    XClusterConfigCreateFormData formData = new XClusterConfigCreateFormData();
    formData.sourceUniverseUUID = source.getUniverseUUID();
    formData.targetUniverseUUID = target.getUniverseUUID();
    formData.name = "Replication-1";
    formData.tables = new HashSet<String>();
    for (TableInfoForm.TableInfoResp tableInfo : resp) {
      formData.tables.add(tableInfo.tableID);
    }
    formData.bootstrapParams = new XClusterConfigCreateFormData.BootstrapParams();
    formData.bootstrapParams.tables = formData.tables;
    formData.bootstrapParams.backupRequestParams =
        new XClusterConfigCreateFormData.BootstrapParams.BootstarpBackupParams();
    formData.bootstrapParams.backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();

    Result result = createXClusterConfig(formData);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo =
        CommissionerBaseTest.waitForTask(UUID.fromString(json.get("taskUUID").asText()));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(source.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(target.getUniverseUUID()));
    verifyYSQL(target);

    NodeDetails details = source.getUniverseDetails().nodeDetailsSet.iterator().next();
    ShellResponse ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            details,
            source,
            YUGABYTE_DB,
            "insert into some_table values (4, 'xcluster1', 200), " + "(5, 'xCluster2', 180)",
            10);
    assertTrue(ysqlResponse.isSuccess());

    Thread.sleep(300);
    details = target.getUniverseDetails().nodeDetailsSet.iterator().next();
    ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            details, target, YUGABYTE_DB, "select count(*) from some_table", 10);
    assertTrue(ysqlResponse.isSuccess());
    assertEquals("5", LocalNodeManager.getRawCommandOutput(ysqlResponse.getMessage()));
    verifyPayload();
  }

  @Test
  public void testXClusterConfigAddTable() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getDefaultUserIntent("source-universe", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe source = createUniverseWithYbc(userIntent);
    initYSQL(source);
    initAndStartPayload(source);

    userIntent = getDefaultUserIntent("target-universe", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe target = createUniverseWithYbc(userIntent);

    // Set up the storage config.
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", baseDir);
    log.info("Customer config here: {}", customerConfig.toString());

    // Get the table info for the source universe.
    List<TableInfoForm.TableInfoResp> resp =
        tableHandler.listTables(
            customer.getUuid(), source.getUniverseUUID(), false, false, true, true);

    XClusterConfigCreateFormData formData = new XClusterConfigCreateFormData();
    formData.sourceUniverseUUID = source.getUniverseUUID();
    formData.targetUniverseUUID = target.getUniverseUUID();
    formData.name = "Replication-1";
    formData.tables = new HashSet<String>();
    for (TableInfoForm.TableInfoResp tableInfo : resp) {
      formData.tables.add(tableInfo.tableID);
    }
    formData.bootstrapParams = new XClusterConfigCreateFormData.BootstrapParams();
    formData.bootstrapParams.tables = formData.tables;
    formData.bootstrapParams.backupRequestParams =
        new XClusterConfigCreateFormData.BootstrapParams.BootstarpBackupParams();
    formData.bootstrapParams.backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();

    Result result = createXClusterConfig(formData);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo =
        CommissionerBaseTest.waitForTask(UUID.fromString(json.get("taskUUID").asText()));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(source.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(target.getUniverseUUID()));
    verifyYSQL(target);

    NodeDetails sourceNodeDetails = source.getUniverseDetails().nodeDetailsSet.iterator().next();
    ShellResponse ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            sourceNodeDetails,
            source,
            YUGABYTE_DB,
            "CREATE TABLE x_cluster (id int, name text, age int, PRIMARY KEY(id, name))",
            10);
    assertTrue(ysqlResponse.isSuccess());

    NodeDetails targetNodeDetails = target.getUniverseDetails().nodeDetailsSet.iterator().next();
    ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            targetNodeDetails,
            target,
            YUGABYTE_DB,
            "CREATE TABLE x_cluster (id int, name text, age int, PRIMARY KEY(id, name))",
            10);
    assertTrue(ysqlResponse.isSuccess());

    // Get the table info for the source universe.
    resp =
        tableHandler.listTables(
            customer.getUuid(), source.getUniverseUUID(), false, false, true, true);
    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = new HashSet<String>();
    for (TableInfoForm.TableInfoResp tableInfo : resp) {
      editFormData.tables.add(tableInfo.tableID);
    }
    editFormData.bootstrapParams = formData.bootstrapParams;
    editFormData.bootstrapParams.tables = editFormData.tables;
    result = editXClusterConfig(editFormData, UUID.fromString(json.get("resourceUUID").asText()));
    json = Json.parse(contentAsString(result));
    taskInfo = CommissionerBaseTest.waitForTask(UUID.fromString(json.get("taskUUID").asText()));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(source.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(target.getUniverseUUID()));
    ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            sourceNodeDetails,
            source,
            YUGABYTE_DB,
            "insert into x_cluster values (4, 'xcluster1', 200), " + "(5, 'xCluster2', 180)",
            10);

    assertTrue(ysqlResponse.isSuccess());
    Thread.sleep(500);
    ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            targetNodeDetails, target, YUGABYTE_DB, "select count(*) from x_cluster", 10);
    assertTrue(ysqlResponse.isSuccess());
    assertEquals("2", LocalNodeManager.getRawCommandOutput(ysqlResponse.getMessage()));
    verifyPayload();
  }
}
