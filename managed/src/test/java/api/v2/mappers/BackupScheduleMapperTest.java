// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import api.v2.models.BackupScheduleDuration;
import api.v2.models.BackupScheduleState;
import api.v2.models.TimeUnitType;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.util.List;
import org.junit.Test;

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
