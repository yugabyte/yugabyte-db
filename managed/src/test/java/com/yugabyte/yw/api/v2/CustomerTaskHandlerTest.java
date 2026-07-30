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

import api.v2.models.AZUpgradeState;
import api.v2.models.TaskPagedQuerySpec;
import api.v2.models.TaskPagedResp;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.tasks.CustomerTaskHandler;
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
}
