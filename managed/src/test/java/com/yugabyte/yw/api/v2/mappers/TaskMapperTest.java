// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

import api.v2.mappers.TaskMapper;
import api.v2.models.AZUpgradeState;
import api.v2.models.Task;
import api.v2.models.TaskInfo;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.forms.CustomerTaskFormData;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.Date;
import java.util.UUID;
import org.junit.Test;
import play.libs.Json;

public class TaskMapperTest {

  @Test
  public void toTask_mapsAllFields() {
    UUID taskUuid = UUID.randomUUID();
    UUID targetUuid = UUID.randomUUID();
    Date createTime = new Date(1_700_000_000_000L);
    Date completionTime = new Date(1_700_000_100_000L);

    CustomerTaskFormData source = new CustomerTaskFormData();
    source.id = taskUuid;
    source.title = "Deleted Universe : test-universe";
    source.target = "Universe";
    source.targetUUID = targetUuid;
    source.type = "Delete";
    source.typeName = "Delete Universe";
    source.correlationId = "corr-123";
    source.details =
        Json.newObject()
            .set("versionNumbers", Json.newObject().put("ybSoftwareVersion", "2.20.0.0"));
    source.createTime = createTime;
    source.completionTime = completionTime;
    source.status = "Success";
    source.percentComplete = 100;
    source.abortable = false;
    source.retryable = true;
    source.canRollback = false;
    source.userEmail = "admin@example.test";

    Task out = TaskMapper.INSTANCE.toTask(source);

    assertNotNull(out.getInfo());
    TaskInfo info = out.getInfo();
    assertEquals("Deleted Universe : test-universe", info.getTitle());
    assertEquals("Universe", info.getTarget());
    assertEquals(targetUuid, info.getTargetUuid());
    assertEquals("Delete", info.getType());
    assertEquals("Delete Universe", info.getTypeName());
    assertEquals("corr-123", info.getCorrelationId());
    assertNotNull(info.getDetails().getVersionNumbers());
    assertEquals("2.20.0.0", info.getDetails().getVersionNumbers().getYbSoftwareVersion());

    assertEquals(taskUuid, info.getUuid());
    assertEquals("Success", info.getStatus());
    assertEquals(100, info.getPercentComplete().intValue());
    assertFalse(info.getAbortable());
    assertEquals(true, info.getRetryable());
    assertFalse(info.getCanRollback());
    assertEquals("admin@example.test", info.getUserEmail());
    assertEquals(
        OffsetDateTime.ofInstant(createTime.toInstant(), ZoneOffset.UTC), info.getCreateTime());
    assertEquals(
        OffsetDateTime.ofInstant(completionTime.toInstant(), ZoneOffset.UTC),
        info.getCompletionTime());
  }

  @Test
  public void toTask_mapsSoftwareUpgradeProgressWithAzUpgradeState() {
    UUID azUuid = UUID.randomUUID();
    UUID clusterUuid = UUID.randomUUID();

    CustomerTaskFormData source = new CustomerTaskFormData();
    source.id = UUID.randomUUID();
    source.title = "Universe : test-universe";
    source.details = softwareUpgradeProgressDetails(azUuid, clusterUuid);

    Task out = TaskMapper.INSTANCE.toTask(source);

    assertNotNull(out.getInfo().getDetails());
    assertNotNull(out.getInfo().getDetails().getSoftwareUpgradeProgress());
    assertEquals(
        1,
        out.getInfo()
            .getDetails()
            .getSoftwareUpgradeProgress()
            .getMasterAzUpgradeStatesList()
            .size());
    AZUpgradeState az =
        out.getInfo()
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

  @Test
  public void toTask_nullDetails() {
    CustomerTaskFormData source = new CustomerTaskFormData();
    source.id = UUID.randomUUID();
    source.title = "task";
    source.details = null;

    Task out = TaskMapper.INSTANCE.toTask(source);

    assertNotNull(out.getInfo());
    assertNull(out.getInfo().getDetails());
  }
}
