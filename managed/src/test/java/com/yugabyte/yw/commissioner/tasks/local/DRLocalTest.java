// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.Util.YUGABYTE_DB;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.LocalNodeManager;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.TableInfoForm;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData;
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
public class DRLocalTest extends DRLocalTestBase {

  @Test
  public void testDrConfigSetup() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getDefaultUserIntent("source-universe", false /*disableTls*/);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe source = createUniverseWithYbc(userIntent);
    initYSQL(source);
    initAndStartPayload(source);

    userIntent = getDefaultUserIntent("target-universe", false /*disableTLS*/);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe target = createUniverseWithYbc(userIntent);

    // Set up the storage config.
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());
    log.info("Customer config here: {}", customerConfig);

    // Get the table info for the source universe.
    List<TableInfoForm.NamespaceInfoResp> resp =
        tableHandler.listNamespaces(customer.getUuid(), source.getUniverseUUID(), false);

    DrConfigCreateForm formData = new DrConfigCreateForm();
    formData.sourceUniverseUUID = source.getUniverseUUID();
    formData.targetUniverseUUID = target.getUniverseUUID();
    formData.name = "DisasterRecovery-1";
    formData.dbScoped = false;
    formData.dbs = new HashSet<>();
    for (TableInfoForm.NamespaceInfoResp namespaceInfo : resp) {
      if (namespaceInfo.name.equals("yugabyte")) {
        formData.dbs.add(namespaceInfo.namespaceUUID.toString());
      }
    }
    formData.bootstrapParams = new XClusterConfigRestartFormData.RestartBootstrapParams();
    formData.bootstrapParams.backupRequestParams =
        new XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams();
    formData.bootstrapParams.backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();

    Result result = createDrConfig(formData);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), source, target);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(source.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(target.getUniverseUUID()));
    Thread.sleep(3000);
    verifyYSQL(target);

    NodeDetails details = source.getUniverseDetails().nodeDetailsSet.iterator().next();
    ShellResponse ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            details,
            source,
            YUGABYTE_DB,
            "insert into some_table values (4, 'dr1', 200), (5, 'dr2', 180)",
            10);
    assertTrue(ysqlResponse.isSuccess());

    Thread.sleep(2500);
    details = target.getUniverseDetails().nodeDetailsSet.iterator().next();
    ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            details, target, YUGABYTE_DB, "select count(*) from some_table", 10);
    assertTrue(ysqlResponse.isSuccess());
    assertEquals("5", LocalNodeManager.getRawCommandOutput(ysqlResponse.getMessage()));
    verifyPayload();

    Result deleteResult = deleteDrConfig(UUID.fromString(json.get("resourceUUID").asText()));
    assertOk(deleteResult);
    JsonNode deleteJson = Json.parse(contentAsString(deleteResult));
    TaskInfo deleteTaskInfo =
        waitForTask(UUID.fromString(deleteJson.get("taskUUID").asText()), source, target);
    assertEquals(TaskInfo.State.Success, deleteTaskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(source.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(target.getUniverseUUID()));
    Thread.sleep(2000);

    details = source.getUniverseDetails().nodeDetailsSet.iterator().next();
    ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            details,
            source,
            YUGABYTE_DB,
            "insert into some_table values (6, 'dr3', 200), (7, 'dr4', 180)",
            10);
    assertTrue(ysqlResponse.isSuccess());

    Thread.sleep(2000);
    details = target.getUniverseDetails().nodeDetailsSet.iterator().next();
    ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            details, target, YUGABYTE_DB, "select count(*) from some_table", 10);
    assertTrue(ysqlResponse.isSuccess());
    assertEquals("5", LocalNodeManager.getRawCommandOutput(ysqlResponse.getMessage()));
  }
}
