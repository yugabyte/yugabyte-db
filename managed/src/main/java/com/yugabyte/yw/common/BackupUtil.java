// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.cronutils.model.CronType.UNIX;
import static com.yugabyte.yw.common.Util.getUUIDRepresentation;
import static java.lang.Math.abs;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.cronutils.model.Cron;
import com.cronutils.model.definition.CronDefinitionBuilder;
import com.cronutils.model.time.ExecutionTime;
import com.cronutils.parser.CronParser;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableList;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.BackupResp;
import com.yugabyte.yw.models.BackupResp.BackupRespBuilder;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.helpers.CustomerConfigConsts;
import com.yugabyte.yw.models.helpers.KeyspaceTablesList;
import com.yugabyte.yw.models.helpers.TaskType;
import java.text.SimpleDateFormat;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneId;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.inject.Inject;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.CommonTypes.TableType;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;
import org.yb.master.MasterTypes.RelationType;

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
  public static final String YBC_BACKUP_IDENTIFIER = "ybc_backup";
  public static final String YB_CLOUD_COMMAND_TYPE = "table";
  public static final String K8S_CERT_PATH = "/opt/certs/yugabyte/";
  public static final String VM_CERT_DIR = "/yugabyte-tls-config/";
  public static final String BACKUP_SCRIPT = "bin/yb_backup.py";
  public static final String REGION_LOCATIONS = "REGION_LOCATIONS";
  public static final String REGION_NAME = "REGION";
  public static final String SNAPSHOT_URL_FIELD = "snapshot_url";
  public static final String UNIVERSE_UUID_IDENTIFIER_STRING =
      "(univ-[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}/)";
  public static final String BACKUP_IDENTIFIER_STRING =
      "(.*?)" + "(yugabyte_backup/)?" + UNIVERSE_UUID_IDENTIFIER_STRING + "((ybc_)?backup-(.*))";
  public static final String YBC_BACKUP_LOCATION_IDENTIFIER_STRING =
      "(/?)" + UNIVERSE_UUID_IDENTIFIER_STRING + "(" + YBC_BACKUP_IDENTIFIER + ")";
  public static final Pattern PATTERN_FOR_YBC_BACKUP_LOCATION =
      Pattern.compile(YBC_BACKUP_LOCATION_IDENTIFIER_STRING);
  public static final List<TaskType> BACKUP_TASK_TYPES =
      ImmutableList.of(TaskType.CreateBackup, TaskType.BackupUniverse, TaskType.MultiTableBackup);

  public static Map<TableType, YQLDatabase> TABLE_TYPE_TO_YQL_DATABASE_MAP;

  static {
    TABLE_TYPE_TO_YQL_DATABASE_MAP = new HashMap<>();
    TABLE_TYPE_TO_YQL_DATABASE_MAP.put(TableType.YQL_TABLE_TYPE, YQLDatabase.YQL_DATABASE_CQL);
    TABLE_TYPE_TO_YQL_DATABASE_MAP.put(TableType.PGSQL_TABLE_TYPE, YQLDatabase.YQL_DATABASE_PGSQL);
  }

  public static TableType getTableType(String tableType) {
    switch (tableType) {
      case "YSQL":
        return TableType.PGSQL_TABLE_TYPE;
      case "YCQL":
        return TableType.YQL_TABLE_TYPE;
      default:
        throw new IllegalArgumentException("Unexpected table type " + tableType);
    }
  }

  /**
   * Use for cases apart from customer configs. Currently used in backup listing API, and fetching
   * list of locations for backup deletion.
   */
  public static class RegionLocations {
    public String REGION;
    public String LOCATION;
    public String HOST_BASE;
  }

  public static void validateBackupCronExpression(String cronExpression)
      throws PlatformServiceException {
    if (getCronExpressionTimeInterval(cronExpression)
        < BackupUtil.MIN_SCHEDULE_DURATION_IN_MILLIS) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Duration between the cron schedules cannot be less than 1 hour");
    }
  }

  public static long getCronExpressionTimeInterval(String cronExpression) {
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
    return duration.getSeconds() * 1000;
  }

  public static void validateBackupFrequency(long frequency) throws PlatformServiceException {
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
            .isFullBackup(backup.getBackupInfo().isFullBackup)
            .universeName(backup.universeName)
            .backupUUID(backup.backupUUID)
            .taskUUID(backup.taskUUID)
            .scheduleUUID(backup.getScheduleUUID())
            .customerUUID(backup.customerUUID)
            .universeUUID(backup.universeUUID)
            .category(backup.category)
            .kmsConfigUUID(backup.getBackupInfo().kmsConfigUUID)
            .storageConfigUUID(backup.storageConfigUUID)
            .isStorageConfigPresent(isStorageConfigPresent)
            .isUniversePresent(isUniversePresent)
            .totalBackupSizeInBytes(Long.valueOf(backup.getBackupInfo().backupSizeInBytes))
            .backupType(backup.getBackupInfo().backupType)
            .storageConfigType(backup.getBackupInfo().storageConfigType)
            .state(backup.state);
    if (backup.getBackupInfo().backupList == null) {
      KeyspaceTablesList kTList =
          KeyspaceTablesList.builder()
              .keyspace(backup.getBackupInfo().getKeyspace())
              .tablesList(backup.getBackupInfo().getTableNames())
              .backupSizeInBytes(Long.valueOf(backup.getBackupInfo().backupSizeInBytes))
              .defaultLocation(backup.getBackupInfo().storageLocation)
              .perRegionLocations(backup.getBackupInfo().regionLocations)
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
                        .defaultLocation(b.storageLocation)
                        .perRegionLocations(b.regionLocations)
                        .build();
                  })
              .collect(Collectors.toSet());
      builder.responseList(kTLists);
    }
    return builder.build();
  }

  // For creating new backup we would set the storage location based on
  // universe UUID and backup UUID.
  // univ-<univ_uuid>/backup-<timestamp>-<something_to_disambiguate_from_yugaware>/table-keyspace
  // .table_name.table_uuid
  public static String formatStorageLocation(BackupTableParams params, boolean isYbc) {
    SimpleDateFormat tsFormat = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss");
    String updatedLocation;
    String backupLabel = isYbc ? YBC_BACKUP_IDENTIFIER : "backup";
    if (params.tableUUIDList != null) {
      updatedLocation =
          String.format(
              "univ-%s/%s-%s-%d/multi-table-%s",
              params.universeUUID,
              backupLabel,
              tsFormat.format(new Date()),
              abs(params.backupUuid.hashCode()),
              params.getKeyspace());
    } else if (params.getTableName() == null && params.getKeyspace() != null) {
      updatedLocation =
          String.format(
              "univ-%s/%s-%s-%d/keyspace-%s",
              params.universeUUID,
              backupLabel,
              tsFormat.format(new Date()),
              abs(params.backupUuid.hashCode()),
              params.getKeyspace());
    } else {
      updatedLocation =
          String.format(
              "univ-%s/%s-%s-%d/table-%s.%s",
              params.universeUUID,
              backupLabel,
              tsFormat.format(new Date()),
              abs(params.backupUuid.hashCode()),
              params.getKeyspace(),
              params.getTableName());
      if (params.tableUUID != null) {
        updatedLocation =
            String.format("%s-%s", updatedLocation, params.tableUUID.toString().replace("-", ""));
      }
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
      BackupTableParams params, UUID customerUUID, BackupCategory category) {
    CustomerConfig customerConfig = CustomerConfig.get(customerUUID, params.storageConfigUUID);
    boolean isYbc = category.equals(BackupCategory.YB_CONTROLLER);
    params.storageLocation = formatStorageLocation(params, isYbc);
    if (customerConfig != null) {
      String backupLocation = null;
      if (customerConfig.name.equals(Util.NFS)) {
        CustomerConfigStorageNFSData configData =
            (CustomerConfigStorageNFSData) customerConfig.getDataObject();
        backupLocation = configData.backupLocation;
        if (category.equals(BackupCategory.YB_CONTROLLER))
          // We allow / as nfs location, so add the check here.
          backupLocation =
              getCloudpathWithConfigSuffix(backupLocation, NFSUtil.DEFAULT_YUGABYTE_NFS_BUCKET);
      } else {
        CustomerConfigStorageData configData =
            (CustomerConfigStorageData) customerConfig.getDataObject();
        backupLocation = configData.backupLocation;
      }
      if (StringUtils.isNotBlank(backupLocation)) {
        params.storageLocation =
            getCloudpathWithConfigSuffix(backupLocation, params.storageLocation);
      }
    }
  }

  public void validateStorageConfigOnBackup(CustomerConfig config, Backup backup) {
    StorageUtil storageUtil = StorageUtil.getStorageUtil(config.name);
    BackupTableParams params = backup.getBackupInfo();
    if (CollectionUtils.isNotEmpty(params.backupList)) {
      for (BackupTableParams tableParams : params.backupList) {
        Map<String, String> keyspaceLocationMap = getKeyspaceLocationMap(tableParams);
        storageUtil.validateStorageConfigOnLocations(config.getDataObject(), keyspaceLocationMap);
      }
    } else {
      Map<String, String> keyspaceLocationMap = getKeyspaceLocationMap(params);
      storageUtil.validateStorageConfigOnLocations(config.getDataObject(), keyspaceLocationMap);
    }
  }

  public void validateStorageConfig(CustomerConfig config) throws PlatformServiceException {
    LOG.info(String.format("Validating storage config %s", config.configName));
    CustomerConfigStorageData configData = (CustomerConfigStorageData) config.getDataObject();
    if (StringUtils.isBlank(configData.backupLocation)) {
      throw new PlatformServiceException(BAD_REQUEST, "Default backup location cannot be empty");
    }
    StorageUtil.getStorageUtil(config.name).validateStorageConfigOnLocations(configData);
  }

  /**
   * Get exact storage location for regional locations, while persisting in backup object
   *
   * @param backupLocation The default location of the backup, containing the md/success file
   * @param configDefaultLocation The default config location
   * @param configRegionLocation The regional location from the config
   * @return
   */
  public static String getExactRegionLocation(String backupLocation, String configRegionLocation) {
    String backupIdentifier = getBackupIdentifier(backupLocation, false);
    String location = getCloudpathWithConfigSuffix(configRegionLocation, backupIdentifier);
    return location;
  }

  /**
   * Check whether backup is taken via YB-Controller.
   *
   * @param backupLocation
   * @return
   */
  public boolean isYbcBackup(String backupLocation) {
    return PATTERN_FOR_YBC_BACKUP_LOCATION.matcher(backupLocation).find();
  }

  /**
   * Returns the univ-<>/backup-<>-<>/some_value extracted from the default backup location or
   * yugabyte_backup/univ-<>/backup-<>-<>/some_value if YBC NFS backup and checkYbcNfs is false. If
   * checkYbcNfs is set to true, it additionally checks for yugabyte_bucket/ in the location and
   * removes it.
   *
   * @param checkYbcNfs Remove default nfs bucket name if true
   * @param defaultbackupLocation The default location of the backup, containing the md/success file
   */
  public static String getBackupIdentifier(String defaultBackupLocation, boolean checkYbcNfs) {
    // Group 1: config prefix
    // Group 2: yugabyte_backup/ NFS bucket
    // Group 3: univ-<uuid>/
    // Group 4: suffix after universe
    // Group 5: ybc_ identifier
    Matcher m = Pattern.compile(BACKUP_IDENTIFIER_STRING).matcher(defaultBackupLocation);
    m.matches();
    String backupIdentifier = m.group(3).concat(m.group(4));
    if (checkYbcNfs || StringUtils.isBlank(m.group(2)) || StringUtils.isBlank(m.group(5))) {
      return backupIdentifier;
    }
    // If NFS backup and checkYbcNfs disabled append
    return StringUtils.startsWith(m.group(1), "/")
        ? m.group(2).concat(backupIdentifier)
        : backupIdentifier;
  }

  public void validateRestoreOverwrites(
      List<BackupStorageInfo> backupStorageInfos, Universe universe)
      throws PlatformServiceException {
    List<TableInfo> tableInfoList = getTableInfosOrEmpty(universe);
    for (BackupStorageInfo backupInfo : backupStorageInfos) {
      if (!backupInfo.backupType.equals(TableType.REDIS_TABLE_TYPE)) {
        if (CollectionUtils.isNotEmpty(backupInfo.tableNameList)) {
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
        } else {
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

  public void validateTables(
      List<UUID> tableUuids, Universe universe, String keyspace, TableType tableType)
      throws PlatformServiceException {

    List<TableInfo> tableInfoList = getTableInfosOrEmpty(universe);
    if (keyspace != null && tableUuids.isEmpty()) {
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
      if (tableInfoList.isEmpty()) {
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

  /**
   * Returns a list of locations in the backup.
   *
   * @param backup The backup to get all locations of.
   * @return List of locations( defaul and regional) for the backup.
   */
  public List<String> getBackupLocations(Backup backup) {
    BackupTableParams backupParams = backup.getBackupInfo();
    List<String> backupLocations = new ArrayList<>();
    Map<String, String> keyspaceLocations = new HashMap<>();
    if (backupParams.backupList != null) {
      for (BackupTableParams params : backupParams.backupList) {
        keyspaceLocations = getKeyspaceLocationMap(params);
        keyspaceLocations.values().forEach(l -> backupLocations.add(l));
      }
    } else {
      keyspaceLocations = getKeyspaceLocationMap(backupParams);
      keyspaceLocations.values().forEach(l -> backupLocations.add(l));
    }
    return backupLocations;
  }

  /**
   * Get exact cloud path after merging storage config suffix and backup identifier.
   *
   * @param storageConfigSuffix Storage config suffix to merge with common cloud directory
   * @param commonDir Backup identifier
   */
  public static String getCloudpathWithConfigSuffix(String storageConfigSuffix, String commonDir) {
    return String.format(
        ((StringUtils.isBlank(storageConfigSuffix) || storageConfigSuffix.endsWith("/"))
            ? "%s%s"
            : "%s/%s"),
        storageConfigSuffix,
        commonDir);
  }

  public static String appendSlash(String l) {
    return l.concat(l.endsWith("/") ? "" : "/");
  }

  /**
   * Return the region-location mapping of a given keyspace/table backup. The default location is
   * mapped to "default_region" as key in map.
   *
   * @param tableParams
   * @return The mapping
   */
  public Map<String, String> getKeyspaceLocationMap(BackupTableParams tableParams) {
    Map<String, String> keyspaceRegionLocations = new HashMap<>();
    if (tableParams != null) {
      keyspaceRegionLocations.put(YbcBackupUtil.DEFAULT_REGION_STRING, tableParams.storageLocation);
      if (CollectionUtils.isNotEmpty(tableParams.regionLocations)) {
        tableParams.regionLocations.forEach(
            rL -> {
              keyspaceRegionLocations.put(rL.REGION, rL.LOCATION);
            });
      }
    }
    return keyspaceRegionLocations;
  }

  public static boolean checkInProgressIncrementalBackup(Backup backup) {
    return Backup.fetchAllBackupsByBaseBackupUUID(backup.customerUUID, backup.backupUUID)
        .stream()
        .anyMatch((b) -> (b.state.equals(BackupState.InProgress)));
  }
}
