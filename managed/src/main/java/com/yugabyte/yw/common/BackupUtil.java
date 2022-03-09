// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.cronutils.model.Cron;
import com.cronutils.model.definition.CronDefinitionBuilder;
import com.cronutils.model.time.ExecutionTime;
import com.cronutils.parser.CronParser;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.BackupResp;
import com.yugabyte.yw.models.BackupResp.BackupRespBuilder;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CustomerConfigConsts;
import com.yugabyte.yw.models.helpers.KeyspaceTablesList;
import java.io.IOException;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneId;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import static com.cronutils.model.CronType.UNIX;
import static play.mvc.Http.Status.BAD_REQUEST;

public class BackupUtil {

  public static final Logger LOG = LoggerFactory.getLogger(BackupUtil.class);

  public static final int EMR_MULTIPLE = 8;
  public static final int BACKUP_PREFIX_LENGTH = 8;
  public static final int TS_FMT_LENGTH = 19;
  public static final int UNIV_PREFIX_LENGTH = 6;
  public static final int UUID_LENGTH = 36;
  public static final long MIN_SCHEDULE_DURATION_IN_SECS = 3600L;
  public static final long MIN_SCHEDULE_DURATION_IN_MILLIS = MIN_SCHEDULE_DURATION_IN_SECS * 1000L;
  public static final String BACKUP_SIZE_FIELD = "backup_size_in_bytes";

  public static final String YB_CLOUD_COMMAND_TYPE = "table";
  public static final String K8S_CERT_PATH = "/opt/certs/yugabyte/";
  public static final String VM_CERT_DIR = "/yugabyte-tls-config/";
  public static final String BACKUP_SCRIPT = "bin/yb_backup.py";
  public static final String REGION_LOCATIONS = "REGION_LOCATIONS";
  public static final String REGION_NAME = "REGION";

  public static class RegionLocations {
    public String REGION;
    public String LOCATION;
  }

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

  public static BackupResp toBackupResp(
      Backup backup, CustomerConfigService customerConfigService) {

    Boolean isStorageConfigPresent = true;
    Boolean isUniversePresent = true;
    try {
      CustomerConfig config =
          customerConfigService.getOrBadRequest(
              backup.customerUUID, backup.getBackupInfo().storageConfigUUID);
    } catch (PlatformServiceException e) {
      isStorageConfigPresent = false;
    }
    try {
      Universe universe = Universe.getOrBadRequest(backup.universeUUID);
    } catch (PlatformServiceException e) {
      isUniversePresent = false;
    }
    Boolean onDemand = (backup.getScheduleUUID() == null);
    BackupRespBuilder builder =
        BackupResp.builder()
            .createTime(backup.getCreateTime())
            .updateTime(backup.getUpdateTime())
            .expiryTime(backup.getExpiry())
            .completionTime(backup.getCompletionTime())
            .onDemand(onDemand)
            .sse(backup.getBackupInfo().sse)
            .universeName(backup.universeName)
            .backupUUID(backup.backupUUID)
            .taskUUID(backup.taskUUID)
            .scheduleUUID(backup.getScheduleUUID())
            .customerUUID(backup.customerUUID)
            .universeUUID(backup.universeUUID)
            .storageConfigUUID(backup.storageConfigUUID)
            .isStorageConfigPresent(isStorageConfigPresent)
            .isUniversePresent(isUniversePresent)
            .totalBackupSizeInBytes(Long.valueOf(backup.getBackupInfo().backupSizeInBytes))
            .backupType(backup.getBackupInfo().backupType)
            .state(backup.state);
    if (backup.getBackupInfo().backupList == null) {
      KeyspaceTablesList kTList =
          KeyspaceTablesList.builder()
              .keyspace(backup.getBackupInfo().getKeyspace())
              .tablesList(backup.getBackupInfo().getTableNames())
              .backupSizeInBytes(Long.valueOf(backup.getBackupInfo().backupSizeInBytes))
              .storageLocation(backup.getBackupInfo().storageLocation)
              .build();
      builder.responseList(Stream.of(kTList).collect(Collectors.toSet()));
    } else {
      Set<KeyspaceTablesList> kTLists =
          backup
              .getBackupInfo()
              .backupList
              .stream()
              .map(
                  b -> {
                    return KeyspaceTablesList.builder()
                        .keyspace(b.getKeyspace())
                        .tablesList(b.getTableNames())
                        .backupSizeInBytes(b.backupSizeInBytes)
                        .storageLocation(b.storageLocation)
                        .build();
                  })
              .collect(Collectors.toSet());
      builder.responseList(kTLists);
    }
    return builder.build();
  }

  public static List<String> getStorageLocationList(JsonNode data) throws PlatformServiceException {
    List<String> locations = new ArrayList<>();
    List<RegionLocations> regionsList = null;
    if (StringUtils.isNotBlank(data.get(CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME).asText())) {
      locations.add(data.get(CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME).asText());
    } else {
      throw new PlatformServiceException(BAD_REQUEST, "Default backup location cannot be empty");
    }
    regionsList = getRegionLocationsList(data);
    if (CollectionUtils.isNotEmpty(regionsList)) {
      locations.addAll(
          regionsList
              .parallelStream()
              .filter(r -> StringUtils.isNotBlank(r.REGION) && StringUtils.isNotBlank(r.LOCATION))
              .map(r -> r.LOCATION)
              .collect(Collectors.toList()));
    }
    return locations;
  }

  public static List<RegionLocations> getRegionLocationsList(JsonNode data) {
    List<RegionLocations> regionLocationsList = null;
    try {
      ObjectMapper mapper = new ObjectMapper();
      String jsonLocations =
          mapper.writeValueAsString(data.get(CustomerConfigConsts.REGION_LOCATIONS_FIELDNAME));
      regionLocationsList = Arrays.asList(mapper.readValue(jsonLocations, RegionLocations[].class));
    } catch (IOException e) {
      LOG.error("Error parsing regionLocations list: ", e);
    }
    return regionLocationsList;
  }

  public static void validateStorageConfigOnLocations(
      CustomerConfig config, List<String> locations) {
    LOG.info(String.format("Validating storage config %s", config.configName));
    Boolean isValid = true;
    switch (config.name) {
      case Util.AZ:
        isValid = AZUtil.canCredentialListObjects(config.data, locations);
        break;
      case Util.GCS:
        isValid = GCPUtil.canCredentialListObjects(config.data, locations);
        break;
      case Util.S3:
        isValid = AWSUtil.canCredentialListObjects(config.data, locations);
        break;
      case Util.NFS:
        isValid = true;
        break;
      default:
        throw new PlatformServiceException(
            BAD_REQUEST, String.format("Invalid config type: %s", config.name));
    }
    if (!isValid) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Storage config %s cannot access location %s",
              config.configName, config.data.get(CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME)));
    }
  }

  public static void validateStorageConfig(CustomerConfig config) throws PlatformServiceException {
    LOG.info(String.format("Validating storage config %s", config.configName));
    Boolean isValid = true;
    switch (config.name) {
      case Util.AZ:
        isValid = AZUtil.canCredentialListObjects(config.data);
        break;
      case Util.GCS:
        isValid = GCPUtil.canCredentialListObjects(config.data);
        break;
      case Util.S3:
        isValid = AWSUtil.canCredentialListObjects(config.data);
        break;
      case Util.NFS:
        isValid = true;
        break;
      default:
        throw new PlatformServiceException(
            BAD_REQUEST, String.format("Invalid config type: %s", config.name));
    }
    if (!isValid) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Storage config %s cannot access location %s",
              config.configName, config.data.get(CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME)));
    }
  }
}
