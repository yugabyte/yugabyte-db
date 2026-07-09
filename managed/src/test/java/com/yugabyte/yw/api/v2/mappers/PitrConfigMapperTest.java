// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

import api.v2.mappers.PitrConfigMapper;
import api.v2.models.PitrConfigInfo;
import api.v2.models.PitrConfigSpec;
import com.yugabyte.yw.common.FakeDBApplication;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.Date;
import java.util.UUID;
import org.junit.Test;
import org.yb.CommonTypes.TableType;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;

public class PitrConfigMapperTest extends FakeDBApplication {

  @Test
  public void toPitrConfig_mapsSpecAndInfo() {
    UUID pitrUuid = UUID.randomUUID();
    UUID customerUuid = UUID.randomUUID();
    Date createTime = new Date(1_700_000_000_000L);
    Date updateTime = new Date(1_700_000_100_000L);

    com.yugabyte.yw.models.PitrConfig source = new com.yugabyte.yw.models.PitrConfig();
    source.setUuid(pitrUuid);
    source.setName("pitr-config-1");
    source.setCustomerUUID(customerUuid);
    source.setDbName("mydb");
    source.setTableType(TableType.PGSQL_TABLE_TYPE);
    source.setScheduleInterval(3600L);
    source.setRetentionPeriod(86400L);
    source.setIntermittentMinRecoverTimeInMillis(100L);
    source.setMinRecoverTimeInMillis(200L);
    source.setMaxRecoverTimeInMillis(300L);
    source.setState(State.COMPLETE);
    source.setCreateTime(createTime);
    source.setUpdateTime(updateTime);
    source.setCreatedForDr(true);
    source.setDisabled(true);

    api.v2.models.PitrConfig out = PitrConfigMapper.INSTANCE.toPitrConfig(source);

    assertNotNull(out.getSpec());
    assertNotNull(out.getInfo());
    PitrConfigSpec spec = out.getSpec();
    PitrConfigInfo info = out.getInfo();

    assertEquals("pitr-config-1", spec.getName());
    assertEquals("mydb", spec.getDbName());
    assertEquals(PitrConfigSpec.TableTypeEnum.PGSQL_TABLE_TYPE, spec.getTableType());
    assertEquals(3600L, spec.getScheduleInterval().longValue());
    assertEquals(86400L, spec.getRetentionPeriod().longValue());
    assertEquals(100L, spec.getIntermittentMinRecoverTimeInMillis().longValue());

    assertEquals(pitrUuid, info.getUuid());
    assertEquals(customerUuid, info.getCustomerUuid());
    assertEquals(200L, info.getMinRecoverTimeInMillis().longValue());
    assertEquals(300L, info.getMaxRecoverTimeInMillis().longValue());
    assertEquals(PitrConfigInfo.StateEnum.COMPLETE, info.getState());
    assertEquals(
        OffsetDateTime.ofInstant(createTime.toInstant(), ZoneOffset.UTC), info.getCreateTime());
    assertEquals(
        OffsetDateTime.ofInstant(updateTime.toInstant(), ZoneOffset.UTC), info.getUpdateTime());
    assertEquals(true, info.getCreatedForDr());
    assertEquals(true, info.getDisabled());
    assertFalse(info.getUsedForXCluster());
  }

  @Test
  public void toPitrConfig_nullTableTypeAndState() {
    com.yugabyte.yw.models.PitrConfig source = new com.yugabyte.yw.models.PitrConfig();
    source.setUuid(UUID.randomUUID());
    source.setCustomerUUID(UUID.randomUUID());
    source.setDbName("db");
    source.setTableType(null);
    source.setState(null);

    api.v2.models.PitrConfig out = PitrConfigMapper.INSTANCE.toPitrConfig(source);

    assertNull(out.getSpec().getTableType());
    assertNull(out.getInfo().getState());
  }
}
