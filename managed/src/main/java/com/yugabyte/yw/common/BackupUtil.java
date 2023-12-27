// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.cronutils.model.Cron;
import com.cronutils.model.definition.CronDefinitionBuilder;
import com.cronutils.model.time.ExecutionTime;
import com.cronutils.parser.CronParser;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.Lists;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.BackupRequestParams.KeyspaceTable;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.BackupResp;
import com.yugabyte.yw.models.BackupResp.BackupRespBuilder;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupVersion;
import com.yugabyte.yw.models.helpers.CustomerConfigConsts;
import com.yugabyte.yw.models.helpers.KeyspaceTablesList;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneId;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.inject.Inject;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.CommonTypes.TableType;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;
import org.yb.master.MasterTypes.RelationType;

import static com.cronutils.model.CronType.UNIX;
import static com.yugabyte.yw.common.Util.getUUIDRepresentation;
import static java.lang.Math.abs;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

@Singleton
public class BackupUtil {

  @Inject YBClientService ybService;

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
  public static final String SNAPSHOT_URL_FIELD = "snapshot_url";

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
      customerConfigService.getOrBadRequest(
          backup.customerUUID, backup.getBackupInfo().storageConfigUUID);
    } catch (PlatformServiceException e) {
      isStorageConfigPresent = false;
    }
    try {
      Universe.getOrBadRequest(backup.universeUUID);
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
            .isFullBackup(backup.getBackupInfo().isFullBackup)
            .universeName(backup.universeName)
            .backupUUID(backup.backupUUID)
            .taskUUID(backup.taskUUID)
            .scheduleUUID(backup.getScheduleUUID())
            .customerUUID(backup.customerUUID)
            .universeUUID(backup.universeUUID)
            .kmsConfigUUID(backup.getBackupInfo().kmsConfigUUID)
            .storageConfigUUID(backup.storageConfigUUID)
            .isStorageConfigPresent(isStorageConfigPresent)
            .isUniversePresent(isUniversePresent)
            .isTableByTableBackup(backup.getBackupInfo().tableByTableBackup)
            .totalBackupSizeInBytes(Long.valueOf(backup.getBackupInfo().backupSizeInBytes))
            .backupType(backup.getBackupInfo().backupType)
            .storageConfigType(backup.getBackupInfo().storageConfigType)
            .state(backup.state);
    List<BackupTableParams> backupParams = backup.getBackupParamsCollection();
    Set<KeyspaceTablesList> kTLists =
        backupParams
            .parallelStream()
            .map(
                b -> {
                  return KeyspaceTablesList.builder()
                      .keyspace(b.getKeyspace())
                      .tablesList(b.getTableNameList())
                      .tableUUIDList(b.getTableUUIDList())
                      .backupSizeInBytes(b.backupSizeInBytes)
                      .defaultLocation(b.storageLocation)
                      .perRegionLocations(b.regionLocations)
                      .build();
                })
            .collect(Collectors.toSet());
    builder.responseList(kTLists);
    return builder.build();
  }

  public List<String> getStorageLocationList(JsonNode data) throws PlatformServiceException {
    List<String> locations = new ArrayList<>();
    List<RegionLocations> regionsList = null;
    if (data.has(CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME)
        && StringUtils.isNotBlank(
            data.get(CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME).asText())) {
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

  public List<RegionLocations> getRegionLocationsList(JsonNode data)
      throws PlatformServiceException {
    List<RegionLocations> regionLocationsList = new ArrayList<>();
    try {
      if (data.has(CustomerConfigConsts.REGION_LOCATIONS_FIELDNAME)) {
        ObjectMapper mapper = new ObjectMapper();
        String jsonLocations =
            mapper.writeValueAsString(data.get(CustomerConfigConsts.REGION_LOCATIONS_FIELDNAME));
        regionLocationsList =
            Arrays.asList(mapper.readValue(jsonLocations, RegionLocations[].class));
      }
    } catch (IOException ex) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Not able to parse region location from the storage config data");
    }

    return regionLocationsList;
  }

  // For creating new backup we would set the storage location based on
  // universe UUID and backup UUID.
  // univ-<univ_uuid>/backup-<timestamp>-<something_to_disambiguate_from_yugaware>/table-keyspace
  // .table_name.table_uuid
  public static String formatStorageLocation(BackupTableParams params, BackupVersion version) {
    SimpleDateFormat tsFormat = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss");
    String updatedLocation;
    if (params.tableUUIDList != null) {
      updatedLocation =
          String.format(
              "univ-%s/backup-%s-%d/multi-table-%s",
              params.universeUUID,
              tsFormat.format(new Date()),
              abs(params.backupUuid.hashCode()),
              params.getKeyspace());
    } else if (params.getTableName() == null && params.getKeyspace() != null) {
      updatedLocation =
          String.format(
              "univ-%s/backup-%s-%d/keyspace-%s",
              params.universeUUID,
              tsFormat.format(new Date()),
              abs(params.backupUuid.hashCode()),
              params.getKeyspace());
    } else {
      updatedLocation =
          String.format(
              "univ-%s/backup-%s-%d/table-%s.%s",
              params.universeUUID,
              tsFormat.format(new Date()),
              abs(params.backupUuid.hashCode()),
              params.getKeyspace(),
              params.getTableName());
      if (params.tableUUID != null) {
        updatedLocation =
            String.format("%s-%s", updatedLocation, params.tableUUID.toString().replace("-", ""));
      }
    }
    if (version.equals(BackupVersion.V2)) {
      updatedLocation = String.format("%s_%s", updatedLocation, params.backupParamsIdentifier);
    }
    return updatedLocation;
  }

  public static List<RegionLocations> extractPerRegionLocationsFromBackupScriptResponse(
      JsonNode backupScriptResponse) {
    ObjectMapper objectMapper = new ObjectMapper();
    List<RegionLocations> regionLocations = new ArrayList<>();
    Map<String, Object> locations = objectMapper.convertValue(backupScriptResponse, Map.class);
    for (Entry<String, Object> entry : locations.entrySet()) {
      if (!(entry.getKey().equals(SNAPSHOT_URL_FIELD)
          || entry.getKey().equals(BACKUP_SIZE_FIELD))) {
        String r = entry.getKey();
        String l = entry.getValue().toString();
        RegionLocations regionLocation = new RegionLocations();
        regionLocation.REGION = r;
        regionLocation.LOCATION = l;
        regionLocations.add(regionLocation);
      }
    }
    return regionLocations;
  }

  public static void updateDefaultStorageLocation(
      BackupTableParams params, UUID customerUUID, BackupVersion version) {
    CustomerConfig customerConfig = CustomerConfig.get(customerUUID, params.storageConfigUUID);
    params.storageLocation = formatStorageLocation(params, version);
    if (customerConfig != null) {
      // TODO: These values, S3 vs NFS / S3_BUCKET vs NFS_PATH come from UI right now...
      JsonNode storageNode =
          customerConfig.getData().get(CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME);
      if (storageNode != null) {
        String storagePath = storageNode.asText();
        if (storagePath != null && !storagePath.isEmpty()) {
          params.storageLocation = String.format("%s/%s", storagePath, params.storageLocation);
        }
      }
    }
  }

  public void validateStorageConfigOnLocations(CustomerConfig config, List<String> locations) {
    LOG.info(String.format("Validating storage config %s", config.configName));
    boolean isValid = true;
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
          String.format("Storage config %s cannot access backup locations", config.configName));
    }
  }

  public void validateStorageConfig(CustomerConfig config) throws PlatformServiceException {
    List<String> locations = null;
    locations = getStorageLocationList(config.getData());
    validateStorageConfigOnLocations(config, locations);
  }

  public static String getExactRegionLocation(
      BackupTableParams backupTableParams, String regionLocation, BackupVersion version) {
    String locationSuffix = formatStorageLocation(backupTableParams, version);
    String location = String.format("%s/%s", regionLocation, locationSuffix);
    return location;
  }

  public void validateRestoreOverwrites(
      List<BackupStorageInfo> backupStorageInfos, Universe universe)
      throws PlatformServiceException {
    List<TableInfo> tableInfoList = getTableInfosOrEmpty(universe);
    for (BackupStorageInfo backupInfo : backupStorageInfos) {
      if (!backupInfo.backupType.equals(TableType.REDIS_TABLE_TYPE)) {
        if (backupInfo.backupType.equals(TableType.YQL_TABLE_TYPE)
            && CollectionUtils.isNotEmpty(backupInfo.tableNameList)) {
          List<TableInfo> tableInfos =
              tableInfoList
                  .parallelStream()
                  .filter(tableInfo -> backupInfo.backupType.equals(tableInfo.getTableType()))
                  .filter(
                      tableInfo -> backupInfo.keyspace.equals(tableInfo.getNamespace().getName()))
                  .filter(tableInfo -> backupInfo.tableNameList.contains(tableInfo.getName()))
                  .collect(Collectors.toList());
          if (CollectionUtils.isNotEmpty(tableInfos)) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "Keyspace %s contains tables with same names, overwriting data is not allowed",
                    backupInfo.keyspace));
          }
        } else if (backupInfo.backupType.equals(TableType.PGSQL_TABLE_TYPE)) {
          List<TableInfo> tableInfos =
              tableInfoList
                  .parallelStream()
                  .filter(tableInfo -> backupInfo.backupType.equals(tableInfo.getTableType()))
                  .filter(
                      tableInfo -> backupInfo.keyspace.equals(tableInfo.getNamespace().getName()))
                  .collect(Collectors.toList());
          if (CollectionUtils.isNotEmpty(tableInfos)) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "Keyspace %s already exists, overwriting data is not allowed",
                    backupInfo.keyspace));
          }
        }
      }
    }
  }

  public void validateBackupRequest(
      List<KeyspaceTable> keyspaceTableList, Universe universe, TableType tableType) {
    if (CollectionUtils.isEmpty(keyspaceTableList)) {
      validateTables(null, universe, null, tableType);
    } else {
      // Verify tables to be backed up are not repeated across parts of request.
      Map<String, Set<UUID>> perKeyspaceTables = new HashMap<>();
      keyspaceTableList
          .stream()
          .forEach(
              kT -> {
                if (perKeyspaceTables.containsKey(kT.keyspace)) {
                  if (CollectionUtils.isEmpty(perKeyspaceTables.get(kT.keyspace))
                      || CollectionUtils.containsAny(
                          perKeyspaceTables.get(kT.keyspace), kT.tableUUIDList)
                      || CollectionUtils.isEmpty(kT.tableUUIDList)) {
                    throw new PlatformServiceException(
                        BAD_REQUEST,
                        String.format(
                            "Repeated tables in backup request for keyspace %s", kT.keyspace));
                  } else {
                    perKeyspaceTables.computeIfPresent(
                        kT.keyspace,
                        (keyspace, tableSet) -> {
                          tableSet.addAll(kT.tableUUIDList);
                          return tableSet;
                        });
                  }
                } else {
                  perKeyspaceTables.put(kT.keyspace, new HashSet<>(kT.tableUUIDList));
                }
              });
      perKeyspaceTables
          .entrySet()
          .stream()
          .forEach(
              entry ->
                  validateTables(
                      Lists.newArrayList(entry.getValue()), universe, entry.getKey(), tableType));
    }
  }

  public void validateTables(
      List<UUID> tableUuids, Universe universe, String keyspace, TableType tableType)
      throws PlatformServiceException {

    List<TableInfo> tableInfoList = getTableInfosOrEmpty(universe);
    if (keyspace != null && CollectionUtils.isEmpty(tableUuids)) {
      tableInfoList =
          tableInfoList
              .parallelStream()
              .filter(tableInfo -> keyspace.equals(tableInfo.getNamespace().getName()))
              .filter(tableInfo -> tableType.equals(tableInfo.getTableType()))
              .collect(Collectors.toList());
      if (tableInfoList.isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot initiate backup with empty Keyspace " + keyspace);
      }
      return;
    }

    if (keyspace == null) {
      tableInfoList =
          tableInfoList
              .parallelStream()
              .filter(tableInfo -> tableType.equals(tableInfo.getTableType()))
              .collect(Collectors.toList());
      if (CollectionUtils.isEmpty(tableInfoList)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "No tables to backup inside specified Universe "
                + universe.universeUUID.toString()
                + " and Table Type "
                + tableType.name());
      }
      return;
    }

    // Match if the table is an index or ysql table.
    for (TableInfo tableInfo : tableInfoList) {
      if (tableUuids.contains(
          getUUIDRepresentation(tableInfo.getId().toStringUtf8().replace("-", "")))) {
        if (tableInfo.hasRelationType()
            && tableInfo.getRelationType() == RelationType.INDEX_TABLE_RELATION) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Cannot backup index table " + tableInfo.getName());
        } else if (tableInfo.hasTableType()
            && tableInfo.getTableType() == TableType.PGSQL_TABLE_TYPE) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Cannot backup ysql table " + tableInfo.getName());
        }
      }
    }
  }

  public List<TableInfo> getTableInfosOrEmpty(Universe universe) throws PlatformServiceException {
    final String masterAddresses = universe.getMasterAddresses(true);
    if (masterAddresses.isEmpty()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Masters are not currently queryable.");
    }
    YBClient client = null;
    try {
      String certificate = universe.getCertificateNodetoNode();
      client = ybService.getClient(masterAddresses, certificate);
      return client.getTablesList().getTableInfoList();
    } catch (Exception e) {
      LOG.warn(e.toString());
      return Collections.emptyList();
    } finally {
      ybService.closeClient(client, masterAddresses);
    }
  }

  public List<String> getBackupLocations(Backup backup) {
    List<String> backupLocations = new ArrayList<>();
    List<BackupTableParams> backupList = backup.getBackupParamsCollection();
    for (BackupTableParams params : backupList) {
      backupLocations.add(params.storageLocation);
      if (CollectionUtils.isNotEmpty(params.regionLocations)) {
        for (RegionLocations regionLocation : params.regionLocations) {
          backupLocations.add(regionLocation.LOCATION);
        }
      }
    }
    return backupLocations;
  }
}
