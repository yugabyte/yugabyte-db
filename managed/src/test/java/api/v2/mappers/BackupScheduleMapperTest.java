// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import api.v2.models.BackupScheduleCronFrequency;
import api.v2.models.BackupScheduleDuration;
import api.v2.models.BackupScheduleFrequencyType;
import api.v2.models.BackupScheduleIntervalFrequency;
import api.v2.models.BackupScheduleKeyspace;
import api.v2.models.BackupScheduleSpec;
import api.v2.models.BackupScheduleState;
import api.v2.models.BackupScheduleTableType;
import api.v2.models.TimeUnitType;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.util.List;
import java.util.UUID;
import org.junit.Test;
import org.yb.CommonTypes.TableType;

public class BackupScheduleMapperTest {

  private static final long MINUTE = 60_000L;
  private static final long DAY = 24 * 60 * MINUTE;

  private static BackupScheduleDuration duration(long amount, TimeUnitType unit) {
    return new BackupScheduleDuration().amount(amount).unit(unit);
  }

  @Test
  public void durationKeepsTheV1UnitWhenItDivides() {
    assertEquals(
        duration(3, TimeUnitType.DAYS), BackupScheduleMapper.toDuration(3 * DAY, TimeUnit.DAYS));
    assertEquals(
        duration(2, TimeUnitType.MONTHS),
        BackupScheduleMapper.toDuration(60 * DAY, TimeUnit.MONTHS));
    assertEquals(
        duration(1, TimeUnitType.YEARS),
        BackupScheduleMapper.toDuration(365 * DAY, TimeUnit.YEARS));
    // A unit that doesn't divide it is replaced by the largest that does.
    assertEquals(
        duration(90, TimeUnitType.MINUTES),
        BackupScheduleMapper.toDuration(90 * MINUTE, TimeUnit.HOURS));
    assertEquals(
        duration(36, TimeUnitType.HOURS), BackupScheduleMapper.toDuration(36 * 60 * MINUTE, null));
    assertEquals(
        duration(1500, TimeUnitType.MILLISECONDS),
        BackupScheduleMapper.toDuration(1500, TimeUnit.SECONDS));
    // Units smaller than a millisecond aren't kept.
    assertEquals(
        duration(2, TimeUnitType.SECONDS),
        BackupScheduleMapper.toDuration(2000, TimeUnit.NANOSECONDS));
    // No duration.
    assertNull(BackupScheduleMapper.toDuration(0, TimeUnit.DAYS));
  }

  @Test
  public void durationsToMillis() {
    assertEquals(90 * MINUTE, BackupScheduleMapper.toMillis(duration(90, TimeUnitType.MINUTES)));
    assertEquals(60 * DAY, BackupScheduleMapper.toMillis(duration(2, TimeUnitType.MONTHS)));
    assertEquals(365 * DAY, BackupScheduleMapper.toMillis(duration(1, TimeUnitType.YEARS)));
    assertNull(BackupScheduleMapper.toV1TimeUnit(duration(1, TimeUnitType.MICROSECONDS)));
    assertThrows(
        IllegalArgumentException.class,
        () -> BackupScheduleMapper.toMillis(duration(1, TimeUnitType.NANOSECONDS)));
    assertThrows(
        ArithmeticException.class,
        () -> BackupScheduleMapper.toMillis(duration(Long.MAX_VALUE, TimeUnitType.DAYS)));
  }

  @Test
  public void specToV1Params() {
    UUID customerUUID = UUID.randomUUID();
    UUID universeUUID = UUID.randomUUID();
    UUID configUUID = UUID.randomUUID();
    UUID tableUUID = UUID.randomUUID();
    BackupScheduleSpec spec =
        new BackupScheduleSpec()
            .name("daily")
            .universeUuid(universeUUID)
            .storageConfigUuid(configUUID)
            .backupType(BackupScheduleTableType.BackupScheduleTableTypeYCQL)
            .keyspaces(
                List.of(new BackupScheduleKeyspace().keyspace("ks").tableUuids(List.of(tableUUID))))
            .frequency(
                new BackupScheduleIntervalFrequency()
                    .frequencyType(BackupScheduleFrequencyType.BackupScheduleFrequencyInterval)
                    .interval(duration(1, TimeUnitType.DAYS)))
            .incrementalInterval(duration(3, TimeUnitType.HOURS))
            .retention(duration(2, TimeUnitType.MONTHS))
            .sse(true)
            .pointInTimeRestore(true);

    BackupRequestParams params = BackupScheduleMapper.toBackupRequestParams(customerUUID, spec);

    assertEquals(customerUUID, params.customerUUID);
    assertEquals("daily", params.scheduleName);
    assertEquals(universeUUID, params.getUniverseUUID());
    assertEquals(configUUID, params.storageConfigUUID);
    assertEquals(TableType.YQL_TABLE_TYPE, params.backupType);
    assertEquals(1, params.keyspaceTableList.size());
    assertEquals("ks", params.keyspaceTableList.get(0).keyspace);
    assertEquals(List.of(tableUUID), params.keyspaceTableList.get(0).tableUUIDList);
    assertEquals(DAY, params.schedulingFrequency);
    assertEquals(TimeUnit.DAYS, params.frequencyTimeUnit);
    assertNull(params.cronExpression);
    assertEquals(3 * 60 * MINUTE, params.incrementalBackupFrequency);
    assertEquals(TimeUnit.HOURS, params.incrementalBackupFrequencyTimeUnit);
    assertEquals(60 * DAY, params.timeBeforeDelete);
    assertEquals(TimeUnit.MONTHS, params.expiryTimeUnit);
    assertTrue(params.sse);
    assertTrue(params.enablePointInTimeRestore);
    assertFalse(params.tableByTableBackup);
    // Unset options keep v1's defaults.
    assertEquals(Util.MIN_NUM_BACKUPS_TO_RETAIN, params.minNumBackupsToRetain);

    BackupRequestParams onACron =
        BackupScheduleMapper.toBackupRequestParams(
            customerUUID,
            spec.keyspaces(null)
                .frequency(
                    new BackupScheduleCronFrequency()
                        .frequencyType(BackupScheduleFrequencyType.BackupScheduleFrequencyCron)
                        .cronExpression("0 2 * * *")));
    assertEquals("0 2 * * *", onACron.cronExpression);
    // The default, as in v1.
    assertTrue(onACron.useLocalTimezone);
    assertEquals(0, onACron.schedulingFrequency);
    // Every keyspace.
    assertEquals(List.of(), onACron.keyspaceTableList);
  }

  @Test
  public void everyV1StateMapsBack() {
    for (Schedule.State state : Schedule.State.values()) {
      BackupScheduleState v2 = BackupScheduleMapper.toBackupScheduleState(state);
      assertEquals(state + " -> " + v2, true, BackupScheduleMapper.toV1States(v2).contains(state));
    }
    assertEquals(
        List.of(Schedule.State.Stopped, Schedule.State.Paused),
        BackupScheduleMapper.toV1States(BackupScheduleState.BackupSchedulePaused));
  }
}
