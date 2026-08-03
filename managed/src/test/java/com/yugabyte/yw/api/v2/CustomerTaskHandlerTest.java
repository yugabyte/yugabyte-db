// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.greaterThanOrEqualTo;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.mvc.Http.Status.NOT_FOUND;

import api.v2.models.AZUpgradeState;
import api.v2.models.TaskPagedQuerySpec;
import api.v2.models.TaskPagedResp;
import api.v2.models.YBATask;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.tasks.CustomerTaskHandler;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PrevYBSoftwareConfig;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Collections;
import java.util.Optional;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class CustomerTaskHandlerTest extends FakeDBApplication {

  private Customer customer;
  private Users user;
  private Universe universe;
  private CustomerTaskHandler handler;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    universe = ModelFactory.createUniverse(customer.getId());
    handler = app.injector().instanceOf(CustomerTaskHandler.class);
  }

  @Test
  public void pageListTasks_returnsPagedRows() {
    ObjectNode responseJson = Json.newObject();
    CustomerTask task =
        createTaskWithStatus(
            universe.getUniverseUUID(),
            CustomerTask.TargetType.Universe,
            CustomerTask.TaskType.Create,
            TaskType.CreateUniverse,
            universe.getName(),
            "Success",
            100.0,
            responseJson);
    when(mockCommissioner.buildTaskStatus(eq(task), any(), any(), any()))
        .thenReturn(Optional.of(responseJson));
    when(mockCommissioner.getUpdatingTaskUUIDsForTargets(any(), any()))
        .thenReturn(Collections.emptyMap());

    TaskPagedQuerySpec spec = new TaskPagedQuerySpec();
    spec.offset(0).limit(10);

    TaskPagedResp resp = handler.pageListTasks(customer.getUuid(), spec);

    assertThat(resp.getTotalCount(), greaterThanOrEqualTo(1));
    assertThat(resp.getEntities().size(), greaterThanOrEqualTo(1));
    assertEquals(task.getTaskUUID(), resp.getEntities().get(0).getInfo().getUuid());
    assertEquals("Universe", resp.getEntities().get(0).getInfo().getTarget());
  }

  @Test
  public void pageListTasks_returnsSoftwareUpgradeProgressWithAzUpgradeState() {
    UUID azUuid = UUID.randomUUID();
    UUID clusterUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    ObjectNode responseJson = Json.newObject();
    CustomerTask task =
        createTaskWithStatus(
            universe.getUniverseUUID(),
            CustomerTask.TargetType.Universe,
            CustomerTask.TaskType.SoftwareUpgradeYB,
            TaskType.SoftwareUpgradeYB,
            universe.getName(),
            "Running",
            50.0,
            responseJson);
    responseJson.set("details", softwareUpgradeProgressDetails(azUuid, clusterUuid));
    when(mockCommissioner.buildTaskStatus(eq(task), any(), any(), any()))
        .thenReturn(Optional.of(responseJson));
    when(mockCommissioner.getUpdatingTaskUUIDsForTargets(any(), any()))
        .thenReturn(Collections.emptyMap());

    TaskPagedQuerySpec spec = new TaskPagedQuerySpec();
    spec.offset(0).limit(10);

    TaskPagedResp resp = handler.pageListTasks(customer.getUuid(), spec);

    assertThat(resp.getEntities().size(), greaterThanOrEqualTo(1));
    assertNotNull(resp.getEntities().get(0).getInfo().getDetails());
    assertNotNull(resp.getEntities().get(0).getInfo().getDetails().getSoftwareUpgradeProgress());
    AZUpgradeState az =
        resp.getEntities()
            .get(0)
            .getInfo()
            .getDetails()
            .getSoftwareUpgradeProgress()
            .getMasterAzUpgradeStatesList()
            .get(0);
    assertEquals(azUuid, az.getAzUuid());
    assertEquals("us-west-1a", az.getAzName());
    assertEquals(clusterUuid, az.getClusterUuid());
    assertEquals(AZUpgradeState.ServerTypeEnum.MASTER, az.getServerType());
    assertEquals(AZUpgradeState.StatusEnum.IN_PROGRESS, az.getStatus());
  }

  @Test
  public void pageListTasks_invalidCustomer() {
    TaskPagedQuerySpec spec = new TaskPagedQuerySpec();
    spec.offset(0).limit(10);

    assertThrows(
        PlatformServiceException.class, () -> handler.pageListTasks(UUID.randomUUID(), spec));
  }

  @Test
  public void pageListTasks_invalidPagination() {
    TaskPagedQuerySpec spec = new TaskPagedQuerySpec();
    spec.offset(-1).limit(10);

    assertThrows(
        PlatformServiceException.class, () -> handler.pageListTasks(customer.getUuid(), spec));
  }

  private static ObjectNode softwareUpgradeProgressDetails(UUID azUuid, UUID clusterUuid) {
    ObjectNode azState =
        Json.newObject()
            .put("azUUID", azUuid.toString())
            .put("azName", "us-west-1a")
            .put("serverType", "MASTER")
            .put("clusterUUID", clusterUuid.toString())
            .put("status", "IN_PROGRESS");

    ObjectNode progress =
        Json.newObject()
            .put("isCanaryUpgrade", false)
            .<ObjectNode>set("masterAZUpgradeStatesList", Json.newArray().add(azState))
            .set("tserverAZUpgradeStatesList", Json.newArray());

    return Json.newObject().set("softwareUpgradeProgress", progress);
  }

  private CustomerTask createTaskWithStatus(
      UUID targetUUID,
      CustomerTask.TargetType targetType,
      CustomerTask.TaskType taskType,
      TaskType taskInfoType,
      String targetName,
      String status,
      double percentComplete,
      ObjectNode responseJson) {
    UUID taskUUID = UUID.randomUUID();
    TestUtils.setFakeHttpContext(user);
    TaskInfo taskInfo = new TaskInfo(taskInfoType, null);
    taskInfo.setUuid(taskUUID);
    taskInfo.setTaskParams(Json.newObject());
    taskInfo.setOwner("");
    taskInfo.save();
    CustomerTask task =
        CustomerTask.create(customer, targetUUID, taskUUID, targetType, taskType, targetName);
    responseJson.put("status", status);
    responseJson.put("percent", percentComplete);
    responseJson.put("abortable", false);
    responseJson.put("retryable", false);
    responseJson.put("canRollback", false);
    return task;
  }

  @Test
  public void rollbackTask_failedSoftwareUpgrade_returnsRollbackYBATask() {
    // Put the universe in the state a failed, rollback-capable software upgrade leaves it in.
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams d = u.getUniverseDetails();
              d.softwareUpgradeState = SoftwareUpgradeState.UpgradeFailed;
              d.isSoftwareRollbackAllowed = true;
              PrevYBSoftwareConfig prev = new PrevYBSoftwareConfig();
              prev.setSoftwareVersion("2.21.0.0-b1");
              d.prevYBSoftwareConfig = prev;
              d.updateInProgress = false;
              u.setUniverseDetails(d);
            });

    SoftwareUpgradeParams params = new SoftwareUpgradeParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.clusters = universe.getUniverseDetails().clusters;
    params.upgradeOption = UpgradeOption.ROLLING_UPGRADE;
    params.ybSoftwareVersion = "2.21.0.0-b2";
    JsonNode paramsJson = Json.toJson(params);

    UUID taskUUID = UUID.randomUUID();
    TaskInfo failed = new TaskInfo(TaskType.SoftwareUpgradeYB, null);
    failed.setUuid(taskUUID);
    failed.setTaskParams(paramsJson);
    failed.setOwner("");
    failed.setYbaVersion(Util.getYbaVersion());
    failed.setTaskState(TaskInfo.State.Failure);
    failed.save();
    CustomerTask.create(
        customer,
        universe.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.SoftwareUpgrade,
        universe.getName());

    // commissioner.submit is mocked, so create the backing TaskInfo the rollback CustomerTask's
    // foreign key references.
    UUID rollbackTaskUUID = UUID.randomUUID();
    TaskInfo rollbackInfo = new TaskInfo(TaskType.RollbackUpgrade, null);
    rollbackInfo.setUuid(rollbackTaskUUID);
    rollbackInfo.setTaskParams(Json.newObject());
    rollbackInfo.setOwner("");
    rollbackInfo.setYbaVersion(Util.getYbaVersion());
    rollbackInfo.setTaskState(TaskInfo.State.Created);
    rollbackInfo.save();

    when(mockCommissioner.canTaskRollbackDetailed(any())).thenReturn(true);
    when(mockCommissioner.getTaskParams(taskUUID)).thenReturn(paramsJson);
    when(mockCommissioner.submit(eq(TaskType.RollbackUpgrade), any())).thenReturn(rollbackTaskUUID);

    YBATask result = handler.rollbackTask(customer.getUuid(), taskUUID);

    // The V2 rollback returns a task handle pointing at the submitted rollback task + universe.
    assertEquals(rollbackTaskUUID, result.getTaskUuid());
    assertEquals(universe.getUniverseUUID(), result.getResourceUuid());
  }

  @Test
  public void rollbackTask_customerNotFound_throwsNotFound() {
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.rollbackTask(UUID.randomUUID(), UUID.randomUUID()));
    assertEquals(NOT_FOUND, ex.getHttpStatus());
  }

  @Test
  public void rollbackTask_taskNotFound_throwsNotFound() {
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.rollbackTask(customer.getUuid(), UUID.randomUUID()));
    assertEquals(NOT_FOUND, ex.getHttpStatus());
  }

  @Test
  public void rollbackTask_notRollbackable_throwsForbidden() {
    UUID taskUUID = UUID.randomUUID();
    TaskInfo taskInfo = new TaskInfo(TaskType.EditUniverse, null);
    taskInfo.setUuid(taskUUID);
    taskInfo.setTaskParams(Json.newObject());
    taskInfo.setOwner("");
    taskInfo.setYbaVersion(Util.getYbaVersion());
    taskInfo.save();
    CustomerTask.create(
        customer,
        universe.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.Update,
        universe.getName());
    when(mockCommissioner.canTaskRollbackDetailed(any())).thenReturn(false);

    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.rollbackTask(customer.getUuid(), taskUUID));
    assertEquals(FORBIDDEN, ex.getHttpStatus());
  }
}
