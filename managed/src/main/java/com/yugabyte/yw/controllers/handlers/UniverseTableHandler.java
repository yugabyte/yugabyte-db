// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static com.yugabyte.yw.common.Util.getUUIDRepresentation;
import static com.yugabyte.yw.forms.TableDefinitionTaskParams.createFromResponse;
import static com.yugabyte.yw.forms.TableInfoForm.NamespaceInfoResp;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.SERVICE_UNAVAILABLE;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateTableSpaces;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteTableFromUniverse;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TableSpaceStructures;
import com.yugabyte.yw.common.TableSpaceUtil;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.table.TableInfoUtil;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.CreateTablespaceParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.TableDefinitionTaskParams;
import com.yugabyte.yw.forms.TableInfoForm.NamespaceInfoResp;
import com.yugabyte.yw.forms.TableInfoForm.TableInfoResp;
import com.yugabyte.yw.forms.TableInfoForm.TablePartitionInfo;
import com.yugabyte.yw.forms.TableInfoForm.TablePartitionInfoKey;
import com.yugabyte.yw.forms.TableInfoForm.TableSizes;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TableDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.BiConsumer;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.CommonTypes;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.ListNamespacesResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;
import org.yb.master.MasterTypes;
import org.yb.master.MasterTypes.RelationType;
import play.Environment;
import play.libs.Json;

@Singleton
@Slf4j
public class UniverseTableHandler {

  private static final Logger LOG = LoggerFactory.getLogger(UniverseTableHandler.class);

  private final Commissioner commissioner;
  private final Config config;
  private final Environment environment;
  private final MetricQueryHelper metricQueryHelper;
  private final NodeUniverseManager nodeUniverseManager;
  private final YBClientService ybClientService;

  private static final String MASTER_LEADER_TIMEOUT_CONFIG_PATH =
      "yb.wait_for_master_leader_timeout";
  private static final String MASTERS_UNAVAILABLE_ERR_MSG =
      "Expected error. Masters are not currently queryable.";
  private static final String PARTITION_QUERY_PATH = "queries/fetch_table_partitions.sql";
  private static final String TABLEGROUP_ID_SUFFIX = ".tablegroup.parent.uuid";
  private static final String COLOCATED_NAME_SUFFIX = ".colocated.parent.tablename";
  private static final String COLOCATION_NAME_SUFFIX = ".colocation.parent.tablename";

  private static final Set<String> PGSQL_SYSTEM_NAMESPACE_LIST =
      new HashSet<>(Arrays.asList("system_platform", "template0", "template1"));
  private static final Set<String> YCQL_SYSTEM_NAMESPACES_LIST =
      new HashSet<>(Arrays.asList("system_schema", "system_auth", "system"));

  @Inject
  public UniverseTableHandler(
      Commissioner commissioner,
      Config config,
      Environment environment,
      MetricQueryHelper metricQueryHelper,
      NodeUniverseManager nodeUniverseManager,
      YBClientService ybClientService) {
    this.commissioner = commissioner;
    this.config = config;
    this.environment = environment;
    this.metricQueryHelper = metricQueryHelper;
    this.nodeUniverseManager = nodeUniverseManager;
    this.ybClientService = ybClientService;
  }

  public List<TableInfoResp> listTables(
      UUID customerUUID,
      UUID universeUUID,
      boolean includeParentTableInfo,
      boolean excludeColocatedTables,
      boolean includeColocatedParentTables,
      boolean xClusterSupportedOnly) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    final String masterAddresses = universe.getMasterAddresses();
    if (masterAddresses.isEmpty()) {
      throw new PlatformServiceException(SERVICE_UNAVAILABLE, MASTERS_UNAVAILABLE_ERR_MSG);
    }

    String certificate = universe.getCertificateNodetoNode();
    ListTablesResponse response =
        listTablesOrBadRequest(masterAddresses, certificate, false /* excludeSystemTables */);
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList =
        response.getTableInfoList();

    return getTableInfoRespFromTableInfo(
        universe,
        tableInfoList,
        includeParentTableInfo,
        excludeColocatedTables,
        includeColocatedParentTables,
        xClusterSupportedOnly);
  }

  /**
   * Converts a list of TableInfo objects into a list of TableInfoResp objects.
   *
   * @param universe The Universe object.
   * @param tableInfoList The list of TableInfo objects.
   * @param includeParentTableInfo Flag indicating whether to include parent table information.
   * @param excludeColocatedTables Flag indicating whether to exclude colocated tables.
   * @param includeColocatedParentTables Flag indicating whether to include colocated parent tables.
   * @param xClusterSupportedOnly Flag indicating whether to include only xCluster supported tables.
   * @return The list of TableInfoResp objects.
   */
  public List<TableInfoResp> getTableInfoRespFromTableInfo(
      Universe universe,
      List<TableInfo> tableInfoList,
      boolean includeParentTableInfo,
      boolean excludeColocatedTables,
      boolean includeColocatedParentTables,
      boolean xClusterSupportedOnly) {
    if (xClusterSupportedOnly && (!includeColocatedParentTables || excludeColocatedTables)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "xClusterSupportedOnly: %b, includeColocatedParentTables: %b,"
                  + " excludeColocatedTables: %b is not supported",
              xClusterSupportedOnly, includeColocatedParentTables, excludeColocatedTables));
    }

    String universeVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    boolean hasColocationInfo =
        CommonUtils.isReleaseBetween("2.18.1.0-b18", "2.19.0.0-b0", universeVersion)
            || CommonUtils.isReleaseEqualOrAfter("2.19.0.0-b168", universeVersion);

    // First filter out all system tables except redis table.
    tableInfoList =
        tableInfoList.stream()
            .filter(
                table -> !TableInfoUtil.isSystemTable(table) || TableInfoUtil.isSystemRedis(table))
            .collect(Collectors.toList());
    List<TableInfoResp> tableInfoRespList = new ArrayList<>(tableInfoList.size());

    // Prepare colocated parent table map.
    Map<TablePartitionInfoKey, MasterDdlOuterClass.ListTablesResponsePB.TableInfo>
        colocatedParentTablesMap =
            tableInfoList.stream()
                .filter(TableInfoUtil::isColocatedParentTable)
                .collect(
                    Collectors.toMap(
                        ti -> new TablePartitionInfoKey(ti.getName(), ti.getNamespace().getName()),
                        Function.identity()));

    // Fetch table partitions information if needed.
    Map<TablePartitionInfoKey, TablePartitionInfo> partitionMap = new HashMap<>();
    Map<TablePartitionInfoKey, TableInfo> tablePartitionInfoToTableInfoMap = new HashMap<>();
    if (includeParentTableInfo) {
      Map<String, List<TableInfo>> namespacesToTablesMap =
          tableInfoList.stream().collect(Collectors.groupingBy(ti -> ti.getNamespace().getName()));
      for (String namespace : namespacesToTablesMap.keySet()) {
        partitionMap.putAll(fetchTablePartitionInfo(universe, namespace));
      }

      tablePartitionInfoToTableInfoMap =
          tableInfoList.stream()
              .collect(
                  Collectors.toMap(
                      ti -> new TablePartitionInfoKey(ti.getName(), ti.getNamespace().getName()),
                      Function.identity()));
    }

    Set<String> excludedKeyspaces = new HashSet<>();

    // Do not allow xcluster replication to be set up for colocated dbs if db rpc does not contain
    // colocated information.
    // Note: excludeColocatedTables effectively means 'exclude tables from colocated keyspaces'.
    if ((!hasColocationInfo && xClusterSupportedOnly) || excludeColocatedTables) {
      excludedKeyspaces.addAll(getColocatedKeySpaces(tableInfoList));
    }

    Map<String, String> indexTableMainTableMap = new HashMap<>();
    Map<String, List<String>> mainTableIndexTablesMap = new HashMap<>();
    boolean hasIndexTable = tableInfoList.stream().anyMatch(TableInfoUtil::isIndexTable);
    // After db versions 2.21.1.0-b168 there exists a new field indexed_table_id in table_info
    // which gives the main table id for an index table directly. Check for the presence of that
    // field in the response to avoid extra rpcs.
    boolean indexedTableIdFieldExists =
        hasIndexTable
            && tableInfoList.stream()
                .anyMatch(tableInfo -> !tableInfo.getIndexedTableId().isEmpty());
    // For xCluster table list, we need to also include the main table uuid for each index table.
    if (xClusterSupportedOnly) {
      if (indexedTableIdFieldExists) {
        LOG.debug("Indexed table id field exists.");
        tableInfoList.forEach(
            tableInfo -> {
              if (!tableInfo.getIndexedTableId().isEmpty()) {
                final String indexTableId = tableInfo.getId().toStringUtf8();
                final String mainTableId = tableInfo.getIndexedTableId();
                indexTableMainTableMap.put(indexTableId, mainTableId);
                mainTableIndexTablesMap
                    .computeIfAbsent(mainTableId, k -> new ArrayList<>())
                    .add(indexTableId);
              }
            });
      } else if (hasIndexTable) {
        LOG.debug("Indexed table id field does not exist. But Index tables are present.");
        mainTableIndexTablesMap.putAll(
            XClusterConfigTaskBase.getMainTableIndexTablesMap(
                this.ybClientService, universe, XClusterConfigTaskBase.getTableIds(tableInfoList)));
        mainTableIndexTablesMap.forEach(
            (mainTable, indexTables) ->
                indexTables.forEach(
                    indexTable -> indexTableMainTableMap.put(indexTable, mainTable)));
      }
    }

    Map<String, TableSizes> tableSizes = getTableSizesOrEmpty(universe);

    for (TableInfo table : tableInfoList) {
      TablePartitionInfoKey tableKey =
          new TablePartitionInfoKey(table.getName(), table.getNamespace().getName());
      if (excludedKeyspaces.contains(table.getNamespace().getName())) {
        continue;
      }
      if (!includeColocatedParentTables && colocatedParentTablesMap.containsKey(tableKey)) {
        // Exclude colocated parent tables unless they are requested explicitly.
        continue;
      }
      TableInfo parentPartitionInfo = null;
      TablePartitionInfo partitionInfo = null;
      if (includeParentTableInfo) {
        if (partitionMap.containsKey(tableKey)) {
          // This 'table' is a partition of some table.
          partitionInfo = partitionMap.get(tableKey);
          parentPartitionInfo =
              tablePartitionInfoToTableInfoMap.get(
                  new TablePartitionInfoKey(partitionInfo.parentTable, partitionInfo.keyspace));
          LOG.debug("Partition {}, Parent {}", partitionInfo, parentPartitionInfo);
        }
      }
      tableInfoRespList.add(
          buildResponseFromTableInfo(
                  table,
                  partitionInfo,
                  parentPartitionInfo,
                  mainTableIndexTablesMap.get(XClusterConfigTaskBase.getTableId(table)),
                  indexTableMainTableMap.get(XClusterConfigTaskBase.getTableId(table)),
                  tableSizes,
                  hasColocationInfo)
              .build());
    }
    if (xClusterSupportedOnly) {
      tableInfoRespList =
          tableInfoRespList.stream()
              .filter(XClusterConfigTaskBase::isXClusterSupported)
              .collect(Collectors.toList());
    }
    return tableInfoRespList;
  }

  public UUID drop(UUID customerUUID, UUID universeUUID, UUID tableUUID) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    final String masterAddresses = universe.getMasterAddresses();
    if (masterAddresses.isEmpty()) {
      throw new PlatformServiceException(SERVICE_UNAVAILABLE, MASTERS_UNAVAILABLE_ERR_MSG);
    }
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = null;

    GetTableSchemaResponse schemaResponse;
    try {
      client = ybClientService.getClient(masterAddresses, certificate);
      schemaResponse = client.getTableSchemaByUUID(tableUUID.toString().replace("-", ""));
    } catch (Exception e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    } finally {
      ybClientService.closeClient(client, masterAddresses);
    }
    if (schemaResponse == null) {
      throw new PlatformServiceException(BAD_REQUEST, "No table for UUID: " + tableUUID);
    }
    DeleteTableFromUniverse.Params taskParams = new DeleteTableFromUniverse.Params();
    taskParams.setUniverseUUID(universeUUID);
    taskParams.expectedUniverseVersion = -1;
    taskParams.tableUUID = tableUUID;
    taskParams.tableName = schemaResponse.getTableName();
    taskParams.keyspace = schemaResponse.getNamespace();
    taskParams.masterAddresses = masterAddresses;
    UUID taskUUID = commissioner.submit(TaskType.DeleteTable, taskParams);
    LOG.info(
        "Submitted delete table for {}:{}, task uuid = {}.",
        taskParams.tableUUID,
        CommonUtils.logTableName(taskParams.getFullName()),
        taskUUID);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Table,
        CustomerTask.TaskType.Delete,
        taskParams.getFullName());
    LOG.info(
        "Saved task uuid {} in customer tasks table for table {}:{}",
        taskUUID,
        taskParams.tableUUID,
        CommonUtils.logTableName(taskParams.getFullName()));
    return taskUUID;
  }

  public UUID create(UUID customerUUID, UUID universeUUID, TableDefinitionTaskParams taskParams) {
    // Validate customer UUID and universe UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    // Submit the task to create the table.
    if (taskParams.tableDetails == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Table details can not be null.");
    }
    TableDetails tableDetails = taskParams.tableDetails;
    UUID taskUUID = commissioner.submit(TaskType.CreateCassandraTable, taskParams);
    LOG.info(
        "Submitted create table for {}:{}, task uuid = {}.",
        taskParams.tableUUID,
        CommonUtils.logTableName(tableDetails.tableName),
        taskUUID);

    // Add this task uuid to the user universe.
    // TODO: check as to why we aren't populating the tableUUID from middleware
    // Which means all the log statements above and below are basically logging null?
    CustomerTask.create(
        customer,
        universe.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Table,
        CustomerTask.TaskType.Create,
        tableDetails.tableName);
    LOG.info(
        "Saved task uuid {} in customer tasks table for table {}:{}.{}",
        taskUUID,
        taskParams.tableUUID,
        tableDetails.keyspace,
        CommonUtils.logTableName(tableDetails.tableName));
    return taskUUID;
  }

  public List<NamespaceInfoResp> listNamespaces(
      UUID customerUUID, UUID universeUUID, boolean includeSystemNamespaces) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    final String masterAddresses = universe.getMasterAddresses();
    if (masterAddresses.isEmpty()) {
      throw new PlatformServiceException(SERVICE_UNAVAILABLE, MASTERS_UNAVAILABLE_ERR_MSG);
    }

    String certificate = universe.getCertificateNodetoNode();
    ListNamespacesResponse response = listNamespacesOrBadRequest(masterAddresses, certificate);
    List<NamespaceInfoResp> namespaceInfoRespList = new ArrayList<>();
    for (MasterTypes.NamespaceIdentifierPB namespace : response.getNamespacesList()) {
      if (includeSystemNamespaces) {
        namespaceInfoRespList.add(NamespaceInfoResp.createFromNamespaceIdentifier(namespace));
      } else if (!((namespace.getDatabaseType().equals(CommonTypes.YQLDatabase.YQL_DATABASE_PGSQL)
              && PGSQL_SYSTEM_NAMESPACE_LIST.contains(namespace.getName().toLowerCase()))
          || (namespace.getDatabaseType().equals(CommonTypes.YQLDatabase.YQL_DATABASE_CQL)
              && YCQL_SYSTEM_NAMESPACES_LIST.contains(namespace.getName().toLowerCase())))) {
        namespaceInfoRespList.add(NamespaceInfoResp.createFromNamespaceIdentifier(namespace));
      }
    }
    return namespaceInfoRespList;
  }

  public TableDefinitionTaskParams describe(UUID customerUUID, UUID universeUUID, UUID tableUUID) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    YBClient client = null;
    String masterAddresses = universe.getMasterAddresses();
    if (masterAddresses.isEmpty()) {
      throw new PlatformServiceException(SERVICE_UNAVAILABLE, MASTERS_UNAVAILABLE_ERR_MSG);
    }
    try {
      String certificate = universe.getCertificateNodetoNode();
      client = ybClientService.getClient(masterAddresses, certificate);
      GetTableSchemaResponse response =
          client.getTableSchemaByUUID(tableUUID.toString().replace("-", ""));
      return createFromResponse(universe, tableUUID, response);
    } catch (IllegalArgumentException e) {
      LOG.error("Failed to get schema of table " + tableUUID + " in universe " + universeUUID, e);
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    } catch (Exception e) {
      LOG.error("Failed to get schema of table " + tableUUID + " in universe " + universeUUID, e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    } finally {
      ybClientService.closeClient(client, masterAddresses);
    }
  }

  public List<TableSpaceStructures.TableSpaceInfo> listTableSpaces(
      UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    final String masterAddresses = universe.getMasterAddresses();
    if (masterAddresses.isEmpty()) {
      return null;
    }

    LOG.info("Fetching table spaces...");
    NodeDetails nodeToUse = null;
    try {
      nodeToUse = CommonUtils.getServerToRunYsqlQuery(universe);
    } catch (IllegalStateException ise) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cluster may not have been initialized yet. Please try later");
    }
    final String fetchTablespaceQuery =
        "select jsonb_agg(t) from (select spcname, spcoptions from pg_catalog.pg_tablespace) as t";
    ShellResponse shellResponse =
        nodeUniverseManager.runYsqlCommand(nodeToUse, universe, "postgres", fetchTablespaceQuery);
    if (!shellResponse.isSuccess()) {
      LOG.warn(
          "Attempt to fetch tablespace info via node {} failed, response {}:{}",
          nodeToUse.nodeName,
          shellResponse.code,
          shellResponse.message);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error while fetching TableSpace information");
    }
    String jsonData = CommonUtils.extractJsonisedSqlResponse(shellResponse);
    List<TableSpaceStructures.TableSpaceInfo> tableSpaceInfoRespList = new ArrayList<>();
    if (jsonData == null || jsonData.isEmpty()) {
      PlatformResults.withData(tableSpaceInfoRespList);
    }

    LOG.debug("jsonData {}", jsonData);
    try {
      ObjectMapper objectMapper = Json.mapper();
      List<TableSpaceStructures.TableSpaceQueryResponse> tablespaceList =
          objectMapper.readValue(
              jsonData, new TypeReference<List<TableSpaceStructures.TableSpaceQueryResponse>>() {});
      tableSpaceInfoRespList =
          tablespaceList.stream()
              .filter(x -> !x.tableSpaceName.startsWith("pg_"))
              .map(TableSpaceUtil::parseToTableSpaceInfo)
              .collect(Collectors.toList());
      return tableSpaceInfoRespList;
    } catch (IOException ioe) {
      LOG.error("Unable to parse fetchTablespaceQuery response {}", jsonData, ioe);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error while fetching TableSpace information");
    }
  }

  public UUID createTableSpaces(
      UUID customerUUID, UUID universeUUID, CreateTablespaceParams tablespacesInfo) {
    // Validate customer UUID.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID.
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    TableSpaceUtil.validateTablespaces(tablespacesInfo, universe);

    CreateTableSpaces.Params taskParams = new CreateTableSpaces.Params();
    taskParams.setUniverseUUID(universeUUID);
    taskParams.tablespaceInfos = tablespacesInfo.tablespaceInfos;
    taskParams.expectedUniverseVersion = universe.getVersion();

    UUID taskUUID = commissioner.submit(TaskType.CreateTableSpacesInUniverse, taskParams);
    LOG.info("Submitted create tablespaces task, uuid = {}.", taskUUID);

    CustomerTask.create(
        customer,
        universe.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.CreateTableSpaces,
        universe.getName());

    LOG.info(
        "Saved task uuid {} in customer tasks table for universe {}:{}.",
        taskUUID,
        universeUUID,
        universe.getName());
    return taskUUID;
  }

  public ListTablesResponse listTablesOrBadRequest(
      String masterAddresses, String certificate, boolean excludeSystemTables) {
    YBClient client = null;
    ListTablesResponse response;
    try {
      client = ybClientService.getClient(masterAddresses, certificate);
      checkLeaderMasterAvailability(client);
      response = client.getTablesList(null, excludeSystemTables, null);
    } catch (Exception e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    } finally {
      ybClientService.closeClient(client, masterAddresses);
    }
    if (response == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Table list can not be empty");
    }
    return response;
  }

  private void checkLeaderMasterAvailability(YBClient client) {
    long waitForLeaderTimeoutMs = config.getDuration(MASTER_LEADER_TIMEOUT_CONFIG_PATH).toMillis();
    try {
      client.waitForMasterLeader(waitForLeaderTimeoutMs);
    } catch (Exception e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Could not find the master leader");
    }
  }

  public ListNamespacesResponse listNamespacesOrBadRequest(
      String masterAddresses, String certificate) {
    YBClient client = null;
    ListNamespacesResponse response;
    try {
      client = ybClientService.getClient(masterAddresses, certificate);
      checkLeaderMasterAvailability(client);
      response = client.getNamespacesList();
    } catch (Exception e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    } finally {
      ybClientService.closeClient(client, masterAddresses);
    }
    if (response == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Table list can not be empty");
    }
    return response;
  }

  // Query prometheus for table sizes.
  private Map<String, TableSizes> getTableSizesOrEmpty(Universe universe) {
    try {
      return queryTableSizes(universe.getUniverseDetails().nodePrefix);
    } catch (RuntimeException e) {
      LOG.error(
          "Error querying for table sizes for universe {} from prometheus",
          universe.getUniverseDetails().nodePrefix,
          e);
    }
    return Collections.emptyMap();
  }

  private Map<String, TableSizes> queryTableSizes(String nodePrefix) {
    HashMap<String, TableSizes> result = new HashMap<>();
    queryAndAppendTableSizeMetric(
        result, "rocksdb_current_version_sst_files_size", nodePrefix, TableSizes::setSstSizeBytes);
    queryAndAppendTableSizeMetric(result, "log_wal_size", nodePrefix, TableSizes::setWalSizeBytes);
    return result;
  }

  private void queryAndAppendTableSizeMetric(
      Map<String, TableSizes> tableSizes,
      String metricName,
      String nodePrefix,
      BiConsumer<TableSizes, Double> fieldSetter) {

    // Execute query and check for errors.
    ArrayList<MetricQueryResponse.Entry> metricValues =
        metricQueryHelper.queryDirect(
            "sum by (table_id) (" + metricName + "{node_prefix=\"" + nodePrefix + "\"})");

    for (final MetricQueryResponse.Entry entry : metricValues) {
      String tableID = entry.labels.get("table_id");
      if (tableID == null
          || tableID.isEmpty()
          || entry.values == null
          || entry.values.size() == 0) {
        continue;
      }
      fieldSetter.accept(
          tableSizes.computeIfAbsent(tableID, k -> new TableSizes()),
          entry.values.get(0).getRight());
    }
  }

  private TableInfoResp.TableInfoRespBuilder buildResponseFromTableInfo(
      TableInfo table,
      TablePartitionInfo tablePartitionInfo,
      TableInfo parentTableInfo,
      List<String> indexTableIds,
      String mainTableUuid,
      Map<String, TableSizes> tableSizeMap,
      boolean hasColocationInfo) {
    String id = table.getId().toStringUtf8();
    String tableKeySpace = table.getNamespace().getName();
    TableInfoResp.TableInfoRespBuilder builder =
        TableInfoResp.builder()
            .tableID(id)
            .tableUUID(getUUIDRepresentation(id))
            .keySpace(tableKeySpace)
            .tableType(table.getTableType())
            .tableName(table.getName())
            .relationType(table.getRelationType())
            .isIndexTable(table.getRelationType() == RelationType.INDEX_TABLE_RELATION);
    TableSizes tableSizes = tableSizeMap.get(id);
    if (tableSizes != null) {
      builder.sizeBytes(tableSizes.getSstSizeBytes());
      builder.walSizeBytes(tableSizes.getWalSizeBytes());
    }
    if (tablePartitionInfo != null) {
      builder.tableSpace(tablePartitionInfo.tablespace);
    }
    if (parentTableInfo != null) {
      builder.parentTableUUID(getUUIDRepresentation(parentTableInfo.getId().toStringUtf8()));
    }
    if (indexTableIds != null) {
      builder.indexTableIDs(indexTableIds);
    }
    if (mainTableUuid != null) {
      builder.mainTableUUID(getUUIDRepresentation(mainTableUuid));
    }
    if (table.hasPgschemaName()) {
      builder.pgSchemaName(table.getPgschemaName());
    }
    if (hasColocationInfo) {
      if (!table.hasColocatedInfo()) {
        builder.colocated(false);
      } else {
        boolean colocated = table.getColocatedInfo().getColocated();
        builder.colocated(colocated);
        if (colocated) {
          if (table.getColocatedInfo().hasParentTableId()) {
            String parentTableId = table.getColocatedInfo().getParentTableId().toStringUtf8();
            builder.colocationParentId(parentTableId);
          }
        }
      }
    }
    return builder;
  }

  private Map<TablePartitionInfoKey, TablePartitionInfo> fetchTablePartitionInfo(
      Universe universe, String dbName) {
    LOG.info("Fetching table partitions...");

    final String fetchPartitionDataQuery =
        FileUtils.readResource(PARTITION_QUERY_PATH, environment);

    NodeDetails randomTServer = CommonUtils.getARandomLiveTServer(universe);
    ShellResponse shellResponse =
        nodeUniverseManager.runYsqlCommand(
            randomTServer, universe, dbName, fetchPartitionDataQuery);
    if (!shellResponse.isSuccess()) {
      LOG.warn(
          "Attempt to fetch table partition info for db {} via node {} failed, response {}:{}",
          dbName,
          randomTServer.nodeName,
          shellResponse.code,
          shellResponse.message);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error while fetching Table Partition information");
    }

    LOG.debug("shell response {}", shellResponse);
    String jsonData = CommonUtils.extractJsonisedSqlResponse(shellResponse);

    if (jsonData == null || (jsonData = jsonData.trim()).isEmpty()) {
      return Collections.EMPTY_MAP;
    }
    LOG.debug("jsonData = {}", jsonData);
    try {
      ObjectMapper objectMapper = Json.mapper();
      List<TablePartitionInfo> partitionList =
          objectMapper.readValue(jsonData, new TypeReference<List<TablePartitionInfo>>() {});
      return partitionList.stream()
          .map(
              partition -> {
                partition.keyspace = dbName;
                return partition;
              })
          .collect(Collectors.toMap(TablePartitionInfo::getKey, Function.identity()));
    } catch (IOException e) {
      LOG.error("Error while parsing partition query response {}", jsonData, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error while fetching Table Partition information");
    }
  }

  private Set<String> getColocatedKeySpaces(List<TableInfo> tableInfoList) {
    Set<String> colocatedKeySpaces = new HashSet<String>();
    for (TableInfo tableInfo : tableInfoList) {
      String keySpace = tableInfo.getNamespace().getName();
      String tableName = tableInfo.getName();
      if (tableName.endsWith(COLOCATED_NAME_SUFFIX) || tableName.endsWith(COLOCATION_NAME_SUFFIX)) {
        colocatedKeySpaces.add(keySpace);
      }
    }
    return colocatedKeySpaces;
  }
}
