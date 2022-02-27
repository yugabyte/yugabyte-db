// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.cronutils.model.Cron;
import com.cronutils.model.definition.CronDefinitionBuilder;
import com.cronutils.model.time.ExecutionTime;
import com.cronutils.parser.CronParser;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.Sets;
import com.yugabyte.yw.forms.BackupTableParams.ActionType;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneId;
import java.util.Set;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import static com.cronutils.model.CronType.UNIX;
import static play.mvc.Http.Status.BAD_REQUEST;

public class BackupUtil {

  public static final Logger LOG = LoggerFactory.getLogger(BackupUtil.class);

  public static final long MIN_SCHEDULE_DURATION_IN_SECS = 3600L;
  public static final long MIN_SCHEDULE_DURATION_IN_MILLIS = MIN_SCHEDULE_DURATION_IN_SECS * 1000L;
  public static final Set<ActionType> OMIT_ACTION_TYPES =
      Sets.immutableEnumSet(ActionType.DELETE, ActionType.RESTORE, ActionType.RESTORE_KEYS);
  public static final String BACKUP_SIZE_FIELD = "backup_size_in_bytes";

  public static void validateBackupCronExpression(String cronExpression)
      throws PlatformServiceException {
    Cron parsedUnixCronExpression;
    try {
      CronParser unixCronParser = new CronParser(CronDefinitionBuilder.instanceDefinitionFor(UNIX));
      parsedUnixCronExpression = unixCronParser.parse(cronExpression);
      parsedUnixCronExpression.validate();
    } catch (Exception ex) {
      throw new PlatformServiceException(BAD_REQUEST, "Cron expression specified is invalid");
    }
    ExecutionTime executionTime = ExecutionTime.forCron(parsedUnixCronExpression);
    Duration timeToNextExecution =
        executionTime.timeToNextExecution(Instant.now().atZone(ZoneId.of("UTC"))).get();
    Duration timeFromLastExecution =
        executionTime.timeFromLastExecution(Instant.now().atZone(ZoneId.of("UTC"))).get();
    Duration duration = Duration.ZERO;
    duration = duration.plus(timeToNextExecution).plus(timeFromLastExecution);
    if (duration.getSeconds() < BackupUtil.MIN_SCHEDULE_DURATION_IN_SECS) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Duration between the cron schedules cannot be less than 1 hour");
    }
  }

  public static void validateBackupFrequency(Long frequency) throws PlatformServiceException {
    if (frequency < MIN_SCHEDULE_DURATION_IN_MILLIS) {
      throw new PlatformServiceException(BAD_REQUEST, "Minimum schedule duration is 1 hour");
    }
  }

  public static long extractBackupSize(JsonNode backupResponse) {
    long backupSize = 0L;
    JsonNode backupSizeJsonNode = backupResponse.get(BACKUP_SIZE_FIELD);
    if (backupSizeJsonNode != null && !backupSizeJsonNode.isNull()) {
      backupSize = Long.parseLong(backupSizeJsonNode.asText());
    } else {
      LOG.error(BACKUP_SIZE_FIELD + " not present in " + backupResponse.toString());
    }
    return backupSize;
  }
}
