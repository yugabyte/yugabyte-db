// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import api.v2.mappers.DrConfigMapper;
import api.v2.models.DrConfigInfo;
import api.v2.models.DrConfigSpec;
import api.v2.models.DrConfigUniverseReplicationState;
import api.v2.models.XClusterTableType;
import com.yugabyte.yw.common.DrConfigStates.SourceUniverseState;
import com.yugabyte.yw.common.DrConfigStates.State;
import com.yugabyte.yw.common.DrConfigStates.TargetUniverseState;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.DrConfigCreateForm.PitrParams;
import com.yugabyte.yw.forms.DrConfigGetResp;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Webhook;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.ArrayList;
import java.util.Date;
import java.util.Set;
import org.junit.Before;
import org.junit.Test;
import org.springframework.test.util.ReflectionTestUtils;

public class DrConfigMapperTest extends FakeDBApplication {

  private static final String TABLE_ID = "000033df000030008000000000004005";
  private static final String WEBHOOK_URL = "http://hook.example/dr";

  private Universe sourceUniverse;
  private Universe targetUniverse;
  private CustomerConfig storageConfig;

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    sourceUniverse = ModelFactory.createUniverse("source-universe", customer.getId());
    targetUniverse = ModelFactory.createUniverse("target-universe", customer.getId());
    storageConfig = ModelFactory.createNfsStorageConfig(customer, "dr-storage");
  }

  @Test
  public void toDrConfig_mapsSpecAndInfo() {
    Date createTime = new Date(1_700_000_000_000L);
    Date modifyTime = new Date(1_700_000_100_000L);
    DrConfig drConfig = createDrConfig(createTime, modifyTime);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    xClusterConfig.setStatus(XClusterConfigStatusType.Running);
    xClusterConfig.setSourceUniverseState(SourceUniverseState.ReplicatingData);
    xClusterConfig.setTargetUniverseState(TargetUniverseState.ReceivingData);
    xClusterConfig.setReplicationGroupName("repl-group");
    xClusterConfig.setKeyspacePending("ks1");
    xClusterConfig.setSourceActive(true);
    xClusterConfig.setTargetActive(false);
    xClusterConfig.setPaused(true);
    xClusterConfig.update();

    Webhook webhook = new Webhook(drConfig, WEBHOOK_URL);
    webhook.save();
    drConfig.setWebhooks(new ArrayList<>());
    drConfig.getWebhooks().add(webhook);
    drConfig.update();
    drConfig.refresh();

    DrConfigGetResp source = new DrConfigGetResp(drConfig, drConfig.getActiveXClusterConfig());
    api.v2.models.DrConfig out = DrConfigMapper.INSTANCE.toDrConfig(source);

    assertNotNull(out.getSpec());
    assertNotNull(out.getInfo());
    DrConfigSpec spec = out.getSpec();
    DrConfigInfo info = out.getInfo();

    assertEquals("dr-test", spec.getName());
    assertEquals(sourceUniverse.getUniverseUUID(), spec.getPrimaryUniverseUuid());
    assertEquals(targetUniverse.getUniverseUUID(), spec.getDrReplicaUniverseUuid());
    assertEquals(XClusterTableType.YSQL, spec.getTableType());
    assertEquals(DrConfigSpec.TypeEnum.TXN, spec.getType());
    assertEquals(86400L, spec.getPitrRetentionPeriodSec().longValue());
    assertEquals(3600L, spec.getPitrSnapshotIntervalSec().longValue());
    assertEquals(Set.of(TABLE_ID), Set.copyOf(spec.getTables()));
    assertNull(spec.getDbs());
    assertNotNull(spec.getBootstrapParams());
    assertNotNull(spec.getBootstrapParams().getBackupRequestParams());
    assertEquals(
        storageConfig.getConfigUUID(),
        spec.getBootstrapParams().getBackupRequestParams().getStorageConfigUuid());
    assertEquals(2, spec.getBootstrapParams().getBackupRequestParams().getParallelism().intValue());
    assertEquals(1, spec.getWebhooks().size());
    assertEquals(WEBHOOK_URL, spec.getWebhooks().get(0).getUrl());

    assertEquals(drConfig.getUuid(), info.getUuid());
    assertEquals(DrConfigInfo.StateEnum.REPLICATING, info.getState());
    assertEquals(DrConfigUniverseReplicationState.REPLICATING_DATA, info.getPrimaryUniverseState());
    assertEquals(
        DrConfigUniverseReplicationState.RECEIVING_DATA_READY_FOR_READS,
        info.getDrReplicaUniverseState());
    assertEquals("ks1", info.getKeyspacePending());
    assertEquals(xClusterConfig.getReplicationGroupName(), info.getReplicationGroupName());
    assertTrue(info.getPrimaryUniverseActive());
    assertFalse(info.getDrReplicaUniverseActive());
    assertEquals(
        OffsetDateTime.ofInstant(createTime.toInstant(), ZoneOffset.UTC), info.getCreateTime());
    assertEquals(
        OffsetDateTime.ofInstant(modifyTime.toInstant(), ZoneOffset.UTC), info.getModifyTime());
  }

  @Test
  public void toDrConfig_mapsAutomaticDdlType() {
    BootstrapBackupParams backupRequestParams = new BootstrapBackupParams();
    backupRequestParams.storageConfigUUID = storageConfig.getConfigUUID();
    backupRequestParams.parallelism = 2;
    DrConfig drConfig =
        DrConfig.create(
            "dr-auto-ddl",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            backupRequestParams,
            new PitrParams(),
            Set.of("namespace-id"),
            true /* isAutomaticDdlMode */);
    drConfig.setWebhooks(new ArrayList<>());
    drConfig.update();

    DrConfigGetResp source = new DrConfigGetResp(drConfig, drConfig.getActiveXClusterConfig());
    api.v2.models.DrConfig out = DrConfigMapper.INSTANCE.toDrConfig(source);

    assertEquals(DrConfigSpec.TypeEnum.AUTO_DDL, out.getSpec().getType());
    assertNull(out.getSpec().getTables());
    assertEquals(Set.of("namespace-id"), Set.copyOf(out.getSpec().getDbs()));
  }

  @Test
  public void toDrConfig_emptyWebhooks() {
    DrConfig drConfig = createDrConfig(new Date(), new Date());
    drConfig.setWebhooks(new ArrayList<>());
    drConfig.update();

    DrConfigGetResp source = new DrConfigGetResp(drConfig, drConfig.getActiveXClusterConfig());
    api.v2.models.DrConfig out = DrConfigMapper.INSTANCE.toDrConfig(source);

    assertNotNull(out.getSpec().getWebhooks());
    assertTrue(out.getSpec().getWebhooks().isEmpty());
  }

  @Test
  public void toDrConfig_nullOptionalEnumsAndDetails() {
    DrConfig drConfig = createDrConfig(new Date(), new Date());
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    // Status and universe states are NOT NULL in DB; set in-memory only for mapper coverage.
    ReflectionTestUtils.setField(xClusterConfig, "status", null);
    ReflectionTestUtils.setField(xClusterConfig, "sourceUniverseState", null);
    ReflectionTestUtils.setField(xClusterConfig, "targetUniverseState", null);
    drConfig.setState(null);
    drConfig.setWebhooks(new ArrayList<>());
    drConfig.update();

    DrConfigGetResp source = new DrConfigGetResp(drConfig, drConfig.getActiveXClusterConfig());
    api.v2.models.DrConfig out = DrConfigMapper.INSTANCE.toDrConfig(source);

    assertNull(out.getInfo().getState());
    assertNull(out.getInfo().getPrimaryUniverseState());
    assertNull(out.getInfo().getDrReplicaUniverseState());
  }

  private DrConfig createDrConfig(Date createTime, Date modifyTime) {
    BootstrapBackupParams backupRequestParams = new BootstrapBackupParams();
    backupRequestParams.storageConfigUUID = storageConfig.getConfigUUID();
    backupRequestParams.parallelism = 2;
    PitrParams pitrParams = new PitrParams();
    pitrParams.retentionPeriodSec = 86400L;
    pitrParams.snapshotIntervalSec = 3600L;

    DrConfig drConfig =
        DrConfig.create(
            "dr-test",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            Set.of(TABLE_ID),
            backupRequestParams,
            pitrParams);
    drConfig.setState(State.Replicating);
    drConfig.setCreateTime(createTime);
    drConfig.setModifyTime(modifyTime);
    drConfig.update();
    return drConfig;
  }
}
