// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.backuprestore;

import static com.cronutils.model.CronType.UNIX;
import static java.lang.Math.max;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.PRECONDITION_FAILED;

import com.cronutils.model.Cron;
import com.cronutils.model.definition.CronDefinitionBuilder;
import com.cronutils.model.time.ExecutionTime;
import com.cronutils.parser.CronParser;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.BiMap;
import com.google.common.collect.HashBiMap;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.forms.RestorePreflightResponse;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Backup.BackupVersion;
import com.yugabyte.yw.models.BackupResp;
import com.yugabyte.yw.models.BackupResp.BackupRespBuilder;
import com.yugabyte.yw.models.CommonBackupInfo;
import com.yugabyte.yw.models.CommonBackupInfo.CommonBackupInfoBuilder;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import com.yugabyte.yw.models.helpers.KeyspaceTablesList;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import com.yugabyte.yw.models.helpers.TaskType;
import io.swagger.annotations.ApiModelProperty;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneId;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;
import java.util.Stack;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import lombok.Builder;
import lombok.Data;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.CommonTypes.TableType;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.client.SnapshotInfo;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;

public class BackupUtil {

  public static final Logger LOG = LoggerFactory.getLogger(BackupUtil.class);

  public static final int EMR_MULTIPLE = 8;
  public static final int BACKUP_PREFIX_LENGTH = 8;
  public static final int TS_FMT_LENGTH = 19;
  public static final int UNIV_PREFIX_LENGTH = 6;
  public static final int UUID_LENGTH = 36;
  public static final int UUID_WITHOUT_HYPHENS_LENGTH = 32;
  public static final int FULL_BACKUP_PREFIX = 6; /* /full/ */
  public static final long MIN_SCHEDULE_DURATION_IN_SECS = 3600L;
  public static final long MIN_SCHEDULE_DURATION_IN_MILLIS = MIN_SCHEDULE_DURATION_IN_SECS * 1000L;
  public static final long MIN_INCREMENTAL_SCHEDULE_DURATION_IN_MILLIS = 1800000L;
  public static final String BACKUP_SIZE_FIELD = "backup_size_in_bytes";
  public static final String YBC_BACKUP_IDENTIFIER = "ybc_backup";
  public static final String YB_CLOUD_COMMAND_TYPE = "table";
  public static final String K8S_CERT_PATH = "/opt/certs/yugabyte/";
  public static final String VM_CERT_DIR = "/yugabyte-tls-config/";
  public static final String BACKUP_SCRIPT = "bin/yb_backup.py";
  public static final String REGION_LOCATIONS = "REGION_LOCATIONS";
  public static final String REGION_NAME = "REGION";
  public static final String SNAPSHOT_URL_FIELD = "snapshot_url";
  public static final String SNAPSHOT_PB = "SnapshotInfoPB";
  public static final String BACKUP_KEYS_JSON = "backup_keys.json";
  public static final String YSQL_DUMP = "YSQLDump";
  public static final String UNIVERSE_UUID_IDENTIFIER_STRING =
      "(univ-[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}/)";
  public static final String BACKUP_IDENTIFIER_STRING =
      "(.*?)" + "(%s)?" + UNIVERSE_UUID_IDENTIFIER_STRING + "((ybc_)?backup-(.*))";
  public static final String YBC_BACKUP_LOCATION_IDENTIFIER_STRING =
      "(/?)" + UNIVERSE_UUID_IDENTIFIER_STRING + "(" + YBC_BACKUP_IDENTIFIER + ")";
  public static final Pattern PATTERN_FOR_YBC_BACKUP_LOCATION =
      Pattern.compile(YBC_BACKUP_LOCATION_IDENTIFIER_STRING);
  public static final List<TaskType> BACKUP_TASK_TYPES =
      ImmutableList.of(TaskType.CreateBackup, TaskType.BackupUniverse, TaskType.MultiTableBackup);

  public static BiMap<TableType, YQLDatabase> TABLE_TYPE_TO_YQL_DATABASE_MAP;

  static {
    TABLE_TYPE_TO_YQL_DATABASE_MAP = HashBiMap.create();
    TABLE_TYPE_TO_YQL_DATABASE_MAP.put(TableType.YQL_TABLE_TYPE, YQLDatabase.YQL_DATABASE_CQL);
    TABLE_TYPE_TO_YQL_DATABASE_MAP.put(TableType.PGSQL_TABLE_TYPE, YQLDatabase.YQL_DATABASE_PGSQL);
    TABLE_TYPE_TO_YQL_DATABASE_MAP.put(TableType.REDIS_TABLE_TYPE, YQLDatabase.YQL_DATABASE_REDIS);
  }

  public enum ApiType {
    YSQL("YSQL"),
    YCQL("YCQL");

    private final String value;

    ApiType(String value) {
      this.value = value;
    }

    public String toString() {
      return this.value;
    }
  }

  public static BiMap<ApiType, TableType> API_TYPE_TO_TABLE_TYPE_MAP = HashBiMap.create();

  static {
    API_TYPE_TO_TABLE_TYPE_MAP.put(ApiType.YSQL, TableType.PGSQL_TABLE_TYPE);
    API_TYPE_TO_TABLE_TYPE_MAP.put(ApiType.YCQL, TableType.YQL_TABLE_TYPE);
  }

  public static String getKeyspaceName(ApiType apiType, String keyspaceName) {
    return apiType.toString().toLowerCase() + "." + keyspaceName;
  }

  public static boolean allSnapshotsSuccessful(List<SnapshotInfo> snapshotInfoList) {
    return !snapshotInfoList.stream().anyMatch(info -> info.getState().equals(State.FAILED));
  }

  public static Metric buildMetricTemplate(
      PlatformMetrics metric, Universe universe, PitrConfig pitrConfig, double value) {
    return MetricService.buildMetricTemplate(
            metric, universe, MetricService.DEFAULT_METRIC_EXPIRY_SEC)
        .setKeyLabel(KnownAlertLabels.PITR_CONFIG_UUID, pitrConfig.getUuid().toString())
        .setLabel(KnownAlertLabels.TABLE_TYPE.labelName(), pitrConfig.getTableType().toString())
        .setLabel(KnownAlertLabels.NAMESPACE_NAME.labelName(), pitrConfig.getDbName())
        .setValue(value);
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

  @Data
  @Builder
  public static class PerLocationBackupInfo {

    @ApiModelProperty(value = "Whether backup type is YSQL")
    @Builder.Default
    private Boolean isYSQLBackup = false;

    @ApiModelProperty(value = "Whether selective table restore is supported for this backup")
    @Builder.Default
    private Boolean isSelectiveRestoreSupported = false;

    @ApiModelProperty(value = "Backup location")
    private String backupLocation;

    @ApiModelProperty(value = "Keyspace and tables list for given backup location")
    @Builder.Default
    private PerBackupLocationKeyspaceTables perBackupLocationKeyspaceTables = null;

    @ApiModelProperty(value = "List of tablespaces in backup")
    @Builder.Default
    private TablespaceResponse tablespaceResponse = null;
  }

  @Data
  @Builder
  public static class TablespaceResponse {
    private boolean containsTablespaces;
    @Builder.Default private List<String> unsupportedTablespaces = null;
    @Builder.Default private List<String> conflictingTablespaces = null;
  }

  @Data
  @Builder
  public static class PerBackupLocationKeyspaceTables {
    @ApiModelProperty(value = "Original keyspace name")
    private String originalKeyspace;

    @ApiModelProperty(value = "List of parent tables associated with the keyspace")
    @Builder.Default
    private List<String> tableNameList = new ArrayList<>();

    @ApiModelProperty(
        hidden = true,
        value = "Used with preflight response generation with backup object, for YBC")
    private Map<String, Set<String>> tablesWithIndexesMap;

    @JsonIgnore
    public Set<String> getAllTables() {
      Set<String> tables = new HashSet<>();
      tables.addAll(tableNameList);
      if (MapUtils.isNotEmpty(tablesWithIndexesMap)) {
        Set<String> indexes =
            tablesWithIndexesMap
                .values()
                .parallelStream()
                .flatMap(tI -> tI.parallelStream())
                .collect(Collectors.toSet());
        tables.addAll(indexes);
      }
      return tables;
    }

    @JsonIgnore
    public Set<String> getIndexesOfTables(Set<String> parentTables) {
      Set<String> indexes = new HashSet<>();
      if (MapUtils.isNotEmpty(tablesWithIndexesMap)) {
        indexes =
            tablesWithIndexesMap
                .entrySet()
                .parallelStream()
                .filter(tWE -> parentTables.contains(tWE.getKey()))
                .flatMap(tWE -> tWE.getValue().parallelStream())
                .collect(Collectors.toSet());
      }
      return indexes;
    }
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

  public static List<BackupStorageInfo> sortBackupStorageInfo(
      List<BackupStorageInfo> backupStorageInfoList) {
    backupStorageInfoList.sort(BackupUtil::compareBackupStorageInfo);
    return backupStorageInfoList;
  }

  public static int compareBackupStorageInfo(
      BackupStorageInfo backupStorageInfo1, BackupStorageInfo backupStorageInfo2) {
    return backupStorageInfo1.storageLocation.compareTo(backupStorageInfo2.storageLocation);
  }

  public static RestoreBackupParams createRestoreKeyParams(
      RestoreBackupParams restoreBackupParams, BackupStorageInfo backupStorageInfo) {
    if (KmsConfig.get(restoreBackupParams.kmsConfigUUID) != null) {
      RestoreBackupParams restoreKeyParams =
          new RestoreBackupParams(
              restoreBackupParams, backupStorageInfo, RestoreBackupParams.ActionType.RESTORE_KEYS);
      return restoreKeyParams;
    }
    return null;
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

  public static BackupResp toBackupResp(Backup backup) {

    Boolean isStorageConfigPresent = checkIfStorageConfigExists(backup);
    Boolean isUniversePresent = checkIfUniverseExists(backup);
    List<Backup> backupChain =
        Backup.fetchAllBackupsByBaseBackupUUID(
            backup.getCustomerUUID(), backup.getBaseBackupUUID());
    Date lastIncrementDate = null;
    boolean hasIncrements = false;
    BackupState lastBackupState = BackupState.Completed;
    if (CollectionUtils.isNotEmpty(backupChain)) {
      lastIncrementDate = backupChain.get(0).getCreateTime();
      lastBackupState = backupChain.get(0).getState();
      hasIncrements = backupChain.size() > 1;
    }
    Boolean onDemand = (backup.getScheduleUUID() == null);
    BackupRespBuilder builder =
        BackupResp.builder()
            .expiryTime(backup.getExpiry())
            .expiryTimeUnit(backup.getExpiryTimeUnit())
            .onDemand(onDemand)
            .isFullBackup(backup.getBackupInfo().isFullBackup)
            .universeName(backup.getUniverseName())
            .scheduleUUID(backup.getScheduleUUID())
            .customerUUID(backup.getCustomerUUID())
            .universeUUID(backup.getUniverseUUID())
            .category(backup.getCategory())
            .isStorageConfigPresent(isStorageConfigPresent)
            .isUniversePresent(isUniversePresent)
            .backupType(backup.getBackupInfo().backupType)
            .storageConfigType(backup.getBackupInfo().storageConfigType)
            .fullChainSizeInBytes(backup.getBackupInfo().fullChainSizeInBytes)
            .commonBackupInfo(getCommonBackupInfo(backup))
            .hasIncrementalBackups(hasIncrements)
            .lastIncrementalBackupTime(lastIncrementDate)
            .lastBackupState(lastBackupState)
            .scheduleName(backup.getScheduleName())
            .useTablespaces(backup.getBackupInfo().useTablespaces);
    return builder.build();
  }

  public static CommonBackupInfo getCommonBackupInfo(Backup backup) {
    CommonBackupInfoBuilder builder = CommonBackupInfo.builder();
    builder
        .createTime(backup.getCreateTime())
        .updateTime(backup.getUpdateTime())
        .completionTime(backup.getCompletionTime())
        .backupUUID(backup.getBackupUUID())
        .baseBackupUUID(backup.getBaseBackupUUID())
        .totalBackupSizeInBytes(Long.valueOf(backup.getBackupInfo().backupSizeInBytes))
        .state(backup.getState())
        .kmsConfigUUID(backup.getBackupInfo().kmsConfigUUID)
        .storageConfigUUID(backup.getStorageConfigUUID())
        .taskUUID(backup.getTaskUUID())
        .sse(backup.getBackupInfo().sse)
        .tableByTableBackup(backup.getBackupInfo().tableByTableBackup);
    List<BackupTableParams> backupParams = backup.getBackupParamsCollection();
    Set<KeyspaceTablesList> kTLists =
        backupParams
            .parallelStream()
            .map(
                b -> {
                  return KeyspaceTablesList.builder()
                      .keyspace(b.getKeyspace())
                      .allTables(b.allTables)
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

  public static List<CommonBackupInfo> getIncrementalBackupList(
      UUID baseBackupUUID, UUID customerUUID) {
    List<Backup> backupChain = Backup.fetchAllBackupsByBaseBackupUUID(customerUUID, baseBackupUUID);
    if (CollectionUtils.isEmpty(backupChain)) {
      return new ArrayList<>();
    }
    return backupChain.stream().map(BackupUtil::getCommonBackupInfo).collect(Collectors.toList());
  }

  /**
   * Generates YBA metadata based suffix for Backup. The format will be: {@code
   * univ-<univ_uuid>/backup-<base_backup_uuid>-<timestamp>/
   * multi-table-<keyspace>_<subtask_param_uuid>}
   *
   * @param params The backup subtask BackupTableParams object
   * @param isYbc If the backup is YBC based
   * @param version Backup version(V1 or V2)
   * @return The suffix generated using metadata
   */
  public static String formatStorageLocation(
      BackupTableParams params, boolean isYbc, BackupVersion version, String backupLocationTS) {
    String updatedLocation;
    String backupLabel = isYbc ? YBC_BACKUP_IDENTIFIER : "backup";
    String fullOrIncrementalLabel =
        params.backupUuid.equals(params.baseBackupUUID) ? "full" : "incremental";
    String backupSubDir =
        String.format(
            "%s-%s/%s/%s",
            backupLabel,
            params.baseBackupUUID.toString().replace("-", ""),
            fullOrIncrementalLabel,
            backupLocationTS);
    String universeSubDir = String.format("univ-%s", params.getUniverseUUID());
    String prefix = String.format("%s/%s", universeSubDir, backupSubDir);
    if (params.tableUUIDList != null) {
      updatedLocation = String.format("%s/multi-table-%s", prefix, params.getKeyspace());
    } else if (params.getTableName() == null && params.getKeyspace() != null) {
      updatedLocation = String.format("%s/keyspace-%s", prefix, params.getKeyspace());
    } else {
      updatedLocation =
          String.format("%s/table-%s.%s", prefix, params.getKeyspace(), params.getTableName());
      if (params.tableUUID != null) {
        updatedLocation =
            String.format("%s-%s", updatedLocation, params.tableUUID.toString().replace("-", ""));
      }
    }
    if (version.equals(BackupVersion.V2)) {
      updatedLocation =
          String.format(
              "%s_%s", updatedLocation, params.backupParamsIdentifier.toString().replace("-", ""));
    }
    return updatedLocation;
  }

  public static List<RegionLocations> extractPerRegionLocationsFromBackupScriptResponse(
      JsonNode backupScriptResponse) {
    ObjectMapper objectMapper = new ObjectMapper();
    List<RegionLocations> regionLocations = new ArrayList<>();
    Map<String, Object> locations =
        objectMapper.convertValue(
            backupScriptResponse, new TypeReference<Map<String, Object>>() {});
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
      BackupTableParams params,
      UUID customerUUID,
      BackupCategory category,
      BackupVersion version,
      String backupLocationTS) {
    CustomerConfig customerConfig = CustomerConfig.get(customerUUID, params.storageConfigUUID);
    boolean isYbc = category.equals(BackupCategory.YB_CONTROLLER);
    params.storageLocation = formatStorageLocation(params, isYbc, version, backupLocationTS);
    if (customerConfig != null) {
      String backupLocation = null;
      if (customerConfig.getName().equals(Util.NFS)) {
        CustomerConfigStorageNFSData configData =
            (CustomerConfigStorageNFSData) customerConfig.getDataObject();
        backupLocation = configData.backupLocation;
        if (category.equals(BackupCategory.YB_CONTROLLER))
          // We allow / as nfs location, so add the check here.
          backupLocation = getPathWithPrefixSuffixJoin(backupLocation, configData.nfsBucket);
      } else {
        CustomerConfigStorageData configData =
            (CustomerConfigStorageData) customerConfig.getDataObject();
        backupLocation = configData.backupLocation;
      }
      if (StringUtils.isNotBlank(backupLocation)) {
        params.storageLocation =
            getPathWithPrefixSuffixJoin(backupLocation, params.storageLocation);
      }
    }
  }

  /**
   * Get exact storage location for regional locations, while persisting in backup object. Used for
   * non-NFS types.
   *
   * @param backupLocation The default location of the backup, containing the md/success file
   * @param configRegionLocation The regional location from the config
   * @return
   */
  public static String getExactRegionLocation(String backupLocation, String configRegionLocation) {
    return getExactRegionLocation(backupLocation, configRegionLocation, "");
  }

  public static String getExactRegionLocation(
      String backupLocation, String configRegionLocation, String nfsBucket) {
    return getPathWithPrefixSuffixJoin(
        configRegionLocation, getBackupIdentifier(backupLocation, false, nfsBucket));
  }

  /**
   * Returns the univ-<>/backup-<>-<>/some_value extracted from the default backup location or
   * NFS_bucket/univ-<>/backup-<>-<>/some_value if YBC NFS backup and checkYbcNfs is false. If
   * checkYbcNfs is set to true, it additionally checks for NFS bucket in the location and removes
   * it.
   *
   * @param checkYbcNfs Remove default nfs bucket name if true
   * @param defaultBackupLocation The default location of the backup, containing the md/success file
   */
  public static String getBackupIdentifier(String defaultBackupLocation, boolean checkYbcNfs) {
    return getBackupIdentifier(defaultBackupLocation, checkYbcNfs, "");
  }

  public static String getBackupIdentifier(
      String defaultBackupLocation, boolean checkYbcNfs, String nfsBucket) {
    String pattern =
        String.format(
            BACKUP_IDENTIFIER_STRING,
            StringUtils.isEmpty(nfsBucket) ? "" : getPathWithPrefixSuffixJoin(nfsBucket, "/"));
    // Group 1: config prefix
    // Group 2: NFS bucket
    // Group 3: univ-<uuid>/
    // Group 4: suffix after universe
    // Group 5: ybc_ identifier
    Matcher m = Pattern.compile(pattern).matcher(defaultBackupLocation);
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

  /**
   * Returns a map of region-"list of locations" across params in the backup.
   *
   * @param backup The backup to get all locations of.
   * @return Map of region-"list of locations" for the backup.
   */
  public static Map<String, List<String>> getBackupLocations(Backup backup) {
    Map<String, List<String>> backupLocationsMap = new HashMap<>();
    Map<String, String> locationsMap = new HashMap<>();
    List<BackupTableParams> bParams = backup.getBackupParamsCollection();
    for (BackupTableParams params : bParams) {
      locationsMap = getLocationMap(params);
      locationsMap.entrySet().stream()
          .forEach(
              e -> {
                backupLocationsMap.computeIfAbsent(
                    e.getKey(),
                    k -> {
                      return new ArrayList<>(Arrays.asList(e.getValue()));
                    });
                backupLocationsMap.computeIfPresent(
                    e.getKey(),
                    (k, v) -> {
                      v.add(e.getValue());
                      return v;
                    });
              });
    }
    return backupLocationsMap;
  }

  /**
   * Merges prefix and suffix with '/' in between. Returns unmodified suffix string if prefix is
   * empty, or unmodified preflix string if suffix is empty.
   *
   * @param prefix The prefix
   * @param suffix Suffix to merge with prefix
   */
  public static String getPathWithPrefixSuffixJoin(String prefix, String suffix) {
    String pFix = StringUtils.isBlank(prefix) ? "" : prefix;
    String sFix = StringUtils.isBlank(suffix) ? "" : suffix;
    if (sFix.startsWith("/") && pFix.endsWith("/")) {
      return String.format("%s%s", StringUtils.chop(pFix), sFix);
    }
    return String.format(
        ((StringUtils.isBlank(pFix)
                || pFix.endsWith("/")
                || StringUtils.isBlank(sFix)
                || sFix.startsWith("/"))
            ? "%s%s"
            : "%s/%s"),
        pFix,
        sFix);
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
  public static Map<String, String> getLocationMap(BackupTableParams tableParams) {
    Map<String, String> regionLocations = new HashMap<>();
    if (tableParams != null) {
      regionLocations.put(YbcBackupUtil.DEFAULT_REGION_STRING, tableParams.storageLocation);
      if (CollectionUtils.isNotEmpty(tableParams.regionLocations)) {
        tableParams.regionLocations.forEach(
            rL -> {
              regionLocations.put(rL.REGION, rL.LOCATION);
            });
      }
    }
    return regionLocations;
  }

  /**
   * Generate backup_location <-> PerLocationBackupInfo map.
   *
   * @param backupParamsList List of backupTableParams
   * @param selectiveRestoreYbcCheck boolean value of whether selective restore is supported on the
   *     target restore universe( ie DB is compatible + this is YBC restore).
   */
  public static Map<String, PerLocationBackupInfo> getBackupLocationBackupInfoMap(
      List<BackupTableParams> backupParamsList,
      boolean selectiveRestoreYbcCheck,
      boolean filterIndexes,
      Map<String, TablespaceResponse> tablespaceResponsesMap) {
    Map<String, PerLocationBackupInfo> backupLocationTablesMap = new HashMap<>();
    backupParamsList.stream()
        .forEach(
            bP -> {
              PerLocationBackupInfo.PerLocationBackupInfoBuilder bInfoBuilder =
                  PerLocationBackupInfo.builder();
              PerBackupLocationKeyspaceTables perLocationKTables =
                  PerBackupLocationKeyspaceTables.builder()
                      .originalKeyspace(bP.getKeyspace())
                      .tableNameList(bP.getTableNameList())
                      .tablesWithIndexesMap(filterIndexes ? null : bP.getTablesWithIndexesMap())
                      .build();
              boolean isYSQLBackup = bP.backupType.equals(TableType.PGSQL_TABLE_TYPE);
              bInfoBuilder
                  .isYSQLBackup(isYSQLBackup)
                  .perBackupLocationKeyspaceTables(perLocationKTables)
                  .backupLocation(bP.storageLocation)
                  .isSelectiveRestoreSupported(selectiveRestoreYbcCheck && !isYSQLBackup)
                  .tablespaceResponse(tablespaceResponsesMap.get(bP.storageLocation));
              backupLocationTablesMap.put(bP.storageLocation, bInfoBuilder.build());
            });
    return backupLocationTablesMap;
  }

  public static String getKeyspaceFromStorageLocation(String storageLocation) {
    String[] splitArray = storageLocation.split("/");
    String keyspaceString = splitArray[(splitArray).length - 1];
    splitArray = keyspaceString.split("-");
    return splitArray[(splitArray).length - 1];
  }

  public static boolean checkInProgressIncrementalBackup(Backup backup) {
    return Backup.fetchAllBackupsByBaseBackupUUID(backup.getCustomerUUID(), backup.getBackupUUID())
        .stream()
        .anyMatch((b) -> (b.getState().equals(BackupState.InProgress)));
  }

  public static boolean checkIfStorageConfigExists(Backup backup) {
    return CustomerConfig.get(backup.getCustomerUUID(), backup.getStorageConfigUUID()) != null;
  }

  public static boolean checkIfUniverseExists(Backup backup) {
    return Universe.maybeGet(backup.getUniverseUUID()).isPresent();
  }

  public static boolean checkIfUniverseExists(UUID universeUUID) {
    return Universe.maybeGet(universeUUID).isPresent();
  }

  /**
   * Function to get total time taken for backups taken in parallel. Does a union of time intervals
   * based upon task-start time time taken, taking into considerations overlapping intervals.
   */
  public static long getTimeTakenForParallelBackups(List<BackupTableParams> backupList) {
    List<Pair<Long, Long>> sortedList =
        backupList.stream()
            .sorted(
                (tP1, tP2) -> {
                  long delta = tP1.thisBackupSubTaskStartTime - tP2.thisBackupSubTaskStartTime;
                  return delta >= 0 ? 1 : -1;
                })
            .map(
                bTP ->
                    new Pair<Long, Long>(
                        bTP.thisBackupSubTaskStartTime,
                        bTP.thisBackupSubTaskStartTime + bTP.timeTakenPartial))
            .collect(Collectors.toList());

    Stack<Pair<Long, Long>> mergeParallelTimes = new Stack<>();
    for (Pair<Long, Long> subTaskTime : sortedList) {
      if (mergeParallelTimes.empty()) {
        mergeParallelTimes.add(subTaskTime);
      } else {
        Pair<Long, Long> peek = mergeParallelTimes.peek();
        if (peek.getSecond() >= subTaskTime.getFirst()) {
          mergeParallelTimes.pop();

          mergeParallelTimes.add(
              new Pair<Long, Long>(
                  peek.getFirst(), max(peek.getSecond(), subTaskTime.getSecond())));
        } else {
          mergeParallelTimes.add(subTaskTime);
        }
      }
    }
    return mergeParallelTimes.stream().mapToLong(p -> p.getSecond() - p.getFirst()).sum();
  }

  // 1. If ysqlBackup then type should be PGSQL
  // 2. If not ysqlBackup, but YBC based backup, type should be YQL
  // 3. If not ysqlBackup, but yb_backup.py  based backup, can be REDIS or YQL
  private static boolean backupTypeMatch(
      boolean isYSQLBackup, TableType backupType, BackupCategory backupCategory) {
    if (isYSQLBackup && backupType.equals(TableType.PGSQL_TABLE_TYPE)) {
      return true;
    }
    if (!isYSQLBackup) {
      if (backupCategory.equals(BackupCategory.YB_BACKUP_SCRIPT)
          && (backupType.equals(TableType.YQL_TABLE_TYPE)
              || backupType.equals(TableType.REDIS_TABLE_TYPE))) {
        return true;
      } else if (backupCategory.equals(BackupCategory.YB_CONTROLLER)
          && (backupType.equals(TableType.YQL_TABLE_TYPE))) {
        return true;
      }
    }
    return false;
  }

  /**
   * Validate Restore backup action with preflight response. Basically use backup metadata to
   * validate restore request, and throw human readable exceptions.
   *
   * @param restoreParams The RestoreBackupParams object
   * @param preflightResponse The restore preflight response object
   */
  public static void validateRestoreActionUsingBackupMetadata(
      RestoreBackupParams restoreParams, RestorePreflightResponse preflightResponse) {
    List<BackupStorageInfo> backupStorageInfoList = restoreParams.backupStorageInfoList;

    // Verify KMS related settings.
    if (restoreParams.kmsConfigUUID == null && preflightResponse.getHasKMSHistory()) {
      throw new PlatformServiceException(
          PRECONDITION_FAILED,
          "The backup has KMS history but no KMS config povided during restore.");
    }

    // Verify backup category and selective restore if any.
    backupStorageInfoList.stream()
        .forEach(
            bSI -> {
              PerLocationBackupInfo bInfo =
                  preflightResponse.getPerLocationBackupInfoMap().get(bSI.storageLocation);

              // YSQL/YCQL backup category section
              boolean isYSQLBackup = bInfo.getIsYSQLBackup();
              if (!backupTypeMatch(
                  isYSQLBackup, /*To verify*/
                  bSI.backupType,
                  preflightResponse.getBackupCategory())) {
                throw new PlatformServiceException(
                    PRECONDITION_FAILED,
                    String.format("Backup category mismatch for location %s", bSI.storageLocation));
              }

              // Tablespaces section
              if (restoreParams.useTablespaces) {
                if (bInfo.tablespaceResponse != null) {
                  List<String> unsupportedTablespaces =
                      bInfo.tablespaceResponse.unsupportedTablespaces;
                  List<String> conflictingTablespaces =
                      bInfo.tablespaceResponse.conflictingTablespaces;
                  if (CollectionUtils.isNotEmpty(unsupportedTablespaces)) {
                    LOG.warn(
                        "Attempting tablespaces restore with unsupported topology: {}",
                        unsupportedTablespaces);
                  }
                  if (CollectionUtils.isNotEmpty(conflictingTablespaces)) {
                    LOG.warn(
                        "Attempting tablespaces restore which already exist on target Universe: {}."
                            + "Note that these will not be overwritten.",
                        unsupportedTablespaces);
                  }
                }
              }

              // Selective table restore section
              if (bSI.selectiveTableRestore) {
                if (!bInfo.getIsSelectiveRestoreSupported()) {
                  throw new PlatformServiceException(
                      PRECONDITION_FAILED,
                      String.format(
                          "Selective restore unsupported for location %s", bSI.storageLocation));
                }
              }
            });
  }
}
