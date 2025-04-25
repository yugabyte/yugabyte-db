package handlers

import (
  "apiserver/cmd/server/helpers"
  "apiserver/cmd/server/models"
  "encoding/json"
  "errors"
  "fmt"
  "github.com/labstack/echo/v4"
  "net/http"
  "strings"
)


const checkpointingState string = "CHECKPOINTING";
const xClusterName string = "xCluster"

// Type definitions for safe time metrics
type XClusterSafeTime struct {
  NamespaceID     string `json:"namespace_id"`
  NamespaceName   string `json:"namespace_name"`
  SafeTime        string `json:"safe_time"`
  SafeTimeEpoch   string `json:"safe_time_epoch"`
  SafeTimeLagSec  string `json:"safe_time_lag_sec"`
  SafeTimeSkewSec string `json:"safe_time_skew_sec"`
}

type XClusterSafeTimeFuture struct {
  Result []XClusterSafeTime
  Error  error
}

type PlacmentLocation struct {
  Cloud  string `json:"cloud"`
  Region string `json:"region"`
  Zone   string `json:"zone"`
}
type UniverseConfigResponse struct {
  Version         int             `json:"version"`
  ReplicationInfo ReplicationInfo `json:"replicationInfo"`
  ClusterUUID     string          `json:"clusterUuid"`
  UniverseUUID    string          `json:"universeUuid"`
}

type ReplicationInfo struct {
  LiveReplicas LiveReplicas `json:"liveReplicas"`
}

type LiveReplicas struct {
  NumReplicas     int              `json:"numReplicas"`
  PlacementBlocks []PlacementBlock `json:"placementBlocks"`
  PlacementUUID   string           `json:"placementUuid"`
}

type PlacementBlock struct {
  CloudInfo      CloudInfo `json:"cloudInfo"`
  MinNumReplicas int       `json:"minNumReplicas"`
}

type CloudInfo struct {
  Cloud  string `json:"placementCloud"`
  Region string `json:"placementRegion"`
  Zone   string `json:"placementZone"`
}

func (c *Container) GetNamespaceByID(nodeHost, namespaceID string) (string, error) {
  future := make(chan helpers.NamespaceFuture)
  go c.helper.GetNamespacesFuture(nodeHost, future)

  namespaceFuture := <-future

  if namespaceFuture.Error != nil {
    return "", namespaceFuture.Error
  }

  for _, ns := range namespaceFuture.NamespaceResponse.UserNamespaces {
    if ns.ID == namespaceID {
      return ns.Name, nil
    }
  }
  for _, ns := range namespaceFuture.NamespaceResponse.SystemNamespaces {
    if ns.ID == namespaceID {
      return ns.Name, nil
    }
  }

  return "", errors.New("namespace not found for the given ID")
}

func (c *Container) GetNamespaceMetrics(ctx echo.Context) error {

  futureXCluster := make(chan helpers.XClusterFuture)
  futureTables := make(chan helpers.TablesFuture)
  namespacesInfoFuture := make(chan helpers.NamespaceFuture)

  go c.helper.GetXClusterFuture(helpers.HOST, futureXCluster)
  go c.helper.GetTablesFuture(helpers.HOST, true, futureTables)
  go c.helper.GetNamespacesFuture(helpers.HOST, namespacesInfoFuture)

  defer close(futureXCluster)
  defer close(futureTables)
  defer close(namespacesInfoFuture)

  xClusterResult := <-futureXCluster
  tablesResult := <-futureTables
  namespacesInfoFutureResponse := <-namespacesInfoFuture

  if xClusterResult.Error != nil {
    c.logger.Errorf("[GetXClusterFuture]: %s", xClusterResult.Error.Error())
    return ctx.String(http.StatusInternalServerError, xClusterResult.Error.Error())
  }
  if tablesResult.Error != nil {
    c.logger.Errorf("[GetTablesFuture]: %s", tablesResult.Error.Error())
    return ctx.String(http.StatusInternalServerError, tablesResult.Error.Error())
  }
  if namespacesInfoFutureResponse.Error != nil {
    c.logger.Errorf("[GetNamespacesFuture]: %s", namespacesInfoFutureResponse.Error.Error())
    return ctx.String(http.StatusInternalServerError, namespacesInfoFutureResponse.Error.Error())
  }

  var replicationID string = strings.TrimSpace(ctx.Param("replication_id"))

  var xClusterNamespaceMetricsResult models.XClusterNamespaceMetrics

  if len(xClusterResult.XClusterResponse.ReplicationInfos) > 0 {
    for _, replicationInfo := range xClusterResult.XClusterResponse.ReplicationInfos {
      if replicationInfo.ReplicationGroupID != replicationID {
        continue
      }

      sourceMastersListFuture := make(chan helpers.MastersListFuture)
      defer close(sourceMastersListFuture)

      // Fetching source node hosts for source universe.
      if len(xClusterResult.XClusterResponse.ConsumerRegistry.ProducerMap) > 0 {
        for _, producerData := range xClusterResult.XClusterResponse.ConsumerRegistry.ProducerMap {
          if len(producerData.Value.MasterAddrs) > 0 {
            go c.helper.GetMastersFromTserverFuture(producerData.Value.MasterAddrs[0].Host,
              sourceMastersListFuture)
            break
          }
        }
      }
      /* So sourceMasterAddressesResponse is an array of such object
         {masterHost: "", isLeader: true/false}
         For getting universe_config via yb-admin even one master_address is sufficient
         So using the 0th index master_address */
      sourceMasterAddressesResponse := <-sourceMastersListFuture
      if sourceMasterAddressesResponse.Error != nil {
        c.logger.Errorf("[GetMastersFromTserverFuture - Source]: %s",
          sourceMasterAddressesResponse.Error.Error())
        return ctx.String(http.StatusInternalServerError,
          sourceMasterAddressesResponse.Error.Error())
      }

      sourcePlacementLocations, sourceUniverseUUID, sourceErr :=
        c.GetPlacementLocationsAndUniverseUUID(sourceMasterAddressesResponse)
      if sourceErr != nil {
        c.logger.Errorf("[GetPlacementLocationsAndUniverseUUID - Source]: %s", sourceErr.Error())
        return ctx.String(http.StatusInternalServerError, sourceErr.Error())
      }

      targetMastersListFuture := make(chan helpers.MastersListFuture)
      defer close(targetMastersListFuture)

      go c.helper.GetMastersFromTserverFuture(helpers.HOST, targetMastersListFuture)
      targetMasterAddressesResponse := <-targetMastersListFuture

      if targetMasterAddressesResponse.Error != nil {
        c.logger.Errorf("[GetMastersFromTserverFuture - Target]: %s",
          targetMasterAddressesResponse.Error.Error())
        return ctx.String(http.StatusInternalServerError,
          targetMasterAddressesResponse.Error.Error())
      }

      targetPlacementLocations, targetUniverseUUID, targetErr :=
        c.GetPlacementLocationsAndUniverseUUID(targetMasterAddressesResponse)
      if targetErr != nil {
        c.logger.Errorf("[GetPlacementLocationsAndUniverseUUID - Target]: %s", targetErr.Error())
        return ctx.String(http.StatusInternalServerError, targetErr.Error())
      }

      namespacesList :=
        make([]models.NamespacesInfo, 0, len(replicationInfo.DbScopedInfo.NamespaceInfos))
      for _, nsInfo := range replicationInfo.DbScopedInfo.NamespaceInfos {
        currentNamespace, err := c.ProcessNamespace(nsInfo, replicationInfo,
            tablesResult, namespacesInfoFutureResponse, ctx,
            xClusterResult.XClusterResponse.ReplicationStatus.Statuses)
        if err != nil {
            c.logger.Errorf("Error processing namespace: %v", err)
            continue
        }
        namespacesList = append(namespacesList, *currentNamespace)
      }

      xClusterNamespaceMetricsResult = models.XClusterNamespaceMetrics{
        NamespaceList:            namespacesList,
        ReplicationGroupId:       replicationID,
        SourcePlacementLocation:  sourcePlacementLocations,
        TargetPlacementLocation:  targetPlacementLocations,
        SourceUniverseUuid:       sourceUniverseUUID,
        TargetUniverseUuid:       targetUniverseUUID,
      }
      break
    }
  }

  return ctx.JSON(http.StatusOK, xClusterNamespaceMetricsResult)
}

// Given a namespace and replicationGroupID, this function retrieves the list of all tables
// within the specified namespace, ensuring that the namespace is part of the replication group.
func (c *Container) ProcessNamespace(
  nsInfo helpers.NamespaceInfo,
  replicationInfo helpers.ReplicationInfo,
  tablesResult helpers.TablesFuture,
  namespacesInfoFutureResponse helpers.NamespaceFuture,
  ctx echo.Context,
  tableIdStreamIdPairsList []helpers.Status,
) (*models.NamespacesInfo, error) {

  xClusterTserverLagMetricsFuture := make(chan helpers.XClusterTserverMetricsResponseFuture)
  defer close(xClusterTserverLagMetricsFuture)

  go c.helper.GetXClusterTserverMetricsFuture(helpers.HOST, xClusterTserverLagMetricsFuture)
  xClusterTserverLagMetrics := <-xClusterTserverLagMetricsFuture

  if xClusterTserverLagMetrics.Error != nil {
    c.logger.Errorf("[ProcessNamespace]: %s", xClusterTserverLagMetrics.Error.Error())
    return nil, ctx.String(http.StatusInternalServerError, xClusterTserverLagMetrics.Error.Error())
  }

  type tServerRequiredMetrics struct {
    avgThroughputKiBps float32
    avgApplyLatencyMs  float32
    avgGetLatencyMs    float32
  }

  xClusterTserverMetricsMap := make(map[string]tServerRequiredMetrics)
  for _, stream := range xClusterTserverLagMetrics.InboundStreams {
    xClusterTserverMetricsMap[stream.StreamID] = tServerRequiredMetrics{
      avgApplyLatencyMs:  stream.AvgApplyLatencyMs,
      avgThroughputKiBps: stream.AvgThroughputKiBps,
      avgGetLatencyMs:    stream.AvgGetChangesLatency,
    }
  }

  var namespace string
  for _, ns := range namespacesInfoFutureResponse.NamespaceResponse.UserNamespaces {
    if ns.ID == nsInfo.ConsumerNamespaceID {
      namespace = ns.Name
      break
    }
  }


  if namespace == "" {
    return nil, fmt.Errorf("namespace not found for ID: %s", nsInfo.ConsumerNamespaceID)
  }

  var tableInfoList []models.XClusterTableInfoInbound
  for _, validatedTable := range replicationInfo.ValidatedTables {
    var tableName string
    for _, t := range tablesResult.Tables.User {
      if strings.TrimSpace(t.Uuid) == strings.TrimSpace(validatedTable.Value) &&
           strings.TrimSpace(t.Keyspace) == strings.TrimSpace(namespace) {
        tableName = t.TableName
        break
      }
    }

    var streamID string
    for _, ts := range tableIdStreamIdPairsList {
      if strings.TrimSpace(ts.TableID) == strings.TrimSpace(validatedTable.Value) {
        streamID = ts.StreamID
        break
      }
    }

    metrics, exists := xClusterTserverMetricsMap[streamID]
    if !exists {
      fmt.Printf("Warning: No metrics found for streamID %s\n", streamID)
    }

    tableInfoList = append(tableInfoList, models.XClusterTableInfoInbound{
      TableUuid:              validatedTable.Value,
      NamespaceId:            nsInfo.ConsumerNamespaceID,
      TableName:              tableName,
      StreamId:               streamID,
      State:                  replicationInfo.State,
      Keyspace:               namespace,
      AvgGetChangesLatencyMs: metrics.avgGetLatencyMs,
      AvgApplyLatencyMs:      metrics.avgApplyLatencyMs,
      AvgThroughputKiBps:     metrics.avgThroughputKiBps,
    })
  }

  return &models.NamespacesInfo{
    Namespace:     namespace,
    TableInfoList: tableInfoList,
  }, nil
}

// Take list of masters as input and return all the placement locations and corresponding
// universe UUID.
func (c *Container) GetPlacementLocationsAndUniverseUUID(
  mastersResponse helpers.MastersListFuture,
) ([]models.XClusterPlacementLocation, string, error) {

  masterAddresses := make([]string, 0, len(mastersResponse.Masters))

  for _, master := range mastersResponse.Masters {
    masterAddresses = append(masterAddresses, master.HostPort)
  }
  masterAddressesCommaSeparated := strings.Join(masterAddresses, ",")

  paramsForYBAdmin := []string{
    "-master_addresses",
    masterAddressesCommaSeparated,
    "get_universe_config",
  }

  universeConfigResponse := make(chan helpers.YBAdminFuture)
  go c.helper.RunYBAdminFuture(paramsForYBAdmin, universeConfigResponse)

  ybAdminFuture := <-universeConfigResponse
  if ybAdminFuture.Error != nil {
    return nil, "", fmt.Errorf("error retrieving universe config: %v", ybAdminFuture.Error)
  }

  var config UniverseConfigResponse
  err := json.Unmarshal([]byte(ybAdminFuture.Result), &config)
  if err != nil {
    return nil, "", fmt.Errorf("error unmarshaling universe config response: %v", err)
  }

  // Getting all the locations and storing it in a set and then converting it a slice.
  // map[models.XClusterPlacementLocation]struct{} using this as a set
  uniqueLocations := make(map[models.XClusterPlacementLocation]struct{})

  for _, placementBlock := range config.ReplicationInfo.LiveReplicas.PlacementBlocks {
    location := models.XClusterPlacementLocation{
      Cloud:  placementBlock.CloudInfo.Cloud,
      Region: placementBlock.CloudInfo.Region,
      Zone:   placementBlock.CloudInfo.Zone,
    }
    uniqueLocations[location] = struct{}{}
  }

  placementLocations := make([]models.XClusterPlacementLocation, 0, len(uniqueLocations))

  for currentLocation := range uniqueLocations {
    placementLocations = append(placementLocations, currentLocation)
  }

  return placementLocations, config.UniverseUUID, nil
}

func ProcessInboundInfoAsync(
  c *Container,
  xClusterAPIResponse helpers.XClusterResponse,
  ctx echo.Context,
  xClusterResponseInbound *[]models.XClusterInboundGroup,
  inboundErrorChan chan error,
) {
  var inboundError error = c.ProcessInboundInfo(xClusterAPIResponse, ctx, xClusterResponseInbound)
  inboundErrorChan <- inboundError
  return
}


func (c *Container) ProcessInboundInfo(
  xClusterAPIResponse helpers.XClusterResponse,
  ctx echo.Context,
  xClusterResponseInbound *[]models.XClusterInboundGroup) error {

  tabletServersFuture := make(chan helpers.TabletServersFuture, 1)
  targetClusterConfigFuture := make(chan helpers.ClusterConfigFuture, 1)

  go func() {
    c.helper.GetTabletServersFuture(helpers.HOST, tabletServersFuture)
    defer close(tabletServersFuture)
  }()

  go func() {
    c.helper.GetClusterConfigFuture(helpers.HOST, targetClusterConfigFuture)
    defer close(targetClusterConfigFuture)
  }()

  tabletServersResponse := <-tabletServersFuture
  targetClusterConfigResponse := <-targetClusterConfigFuture
  targetSideNodeList := c.helper.GetNodesList(tabletServersResponse)

  c.logger.Infof("[ProcessInboundInfo]: Target universe UUID: %s",
    targetClusterConfigResponse.ClusterConfig.UniverseUuid)

  for _, group := range xClusterAPIResponse.ReplicationInfos {
    uniqueSourceSideNodeIps := make(map[string]struct{})

    // Collects all source-side node hosts into a set, which is later converted into a slice.
    for _, producerData := range xClusterAPIResponse.ConsumerRegistry.ProducerMap {
      if len(producerData.Value.MasterAddrs) > 0 {
        for _, masterNode := range producerData.Value.MasterAddrs {
          trimmedHost := strings.TrimSpace(masterNode.Host)
          uniqueSourceSideNodeIps[trimmedHost] = struct{}{}
        }
      }
    }

    sourceSideNodeList := make([]string, 0, len(uniqueSourceSideNodeIps))
    for sourceCurrentNode := range uniqueSourceSideNodeIps {
      sourceSideNodeList = append(sourceSideNodeList, sourceCurrentNode)
    }

    if len(sourceSideNodeList) == 0 {
      c.logger.Errorf("[ProcessInboundInfo]: No node available in source universe")
      return ctx.String(http.StatusInternalServerError, "No nodes available in source universe")
    }

    var firstNodeHostFromSource string = sourceSideNodeList[0]

    sourceClusterConfigFuture := make(chan helpers.ClusterConfigFuture, 1)

    // Using this go-routine instead of cached one, because this needs to be specifically
    // requested on source side to retrieve the source universe UUID.
    go func() {
      c.helper.FetchClusterConfigAsync(firstNodeHostFromSource, sourceClusterConfigFuture)
      defer close(sourceClusterConfigFuture)
    }()

    sourceClusterConfigResponse := <-sourceClusterConfigFuture

    c.logger.Infof("[ProcessInboundInfo]: Source universe UUID: %s",
      sourceClusterConfigResponse.ClusterConfig.UniverseUuid)

    xClusterInboundResponse := models.XClusterInboundGroup{
      ReplicationGroupId: group.ReplicationGroupID,
      State: group.State,
      TargetClusterNodeIps: targetSideNodeList,
      SourceClusterNodeIps: sourceSideNodeList,
      SourceUniverseUuid: sourceClusterConfigResponse.ClusterConfig.UniverseUuid,
      TargetUniverseUuid: targetClusterConfigResponse.ClusterConfig.UniverseUuid,
    }

    *xClusterResponseInbound = append(*xClusterResponseInbound, xClusterInboundResponse)
  }
  return nil
}

func ProcessOutboundInfoAsync(c *Container, xClusterAPIResponse helpers.XClusterResponse,
  ctx echo.Context, xClusterResponseOutbound *[]models.XClusterOutboundGroup,
  outboundErrorChan chan error) {
  var outboundError error =
    c.ProcessOutboundInfo(xClusterAPIResponse, ctx, xClusterResponseOutbound)
  outboundErrorChan <- outboundError
  return
}

func (c *Container) ProcessOutboundInfo(xClusterAPIResponse helpers.XClusterResponse,
  ctx echo.Context, xClusterResponseOutbound *[]models.XClusterOutboundGroup) error {
  if len(xClusterAPIResponse.OutboundReplicationGroups) > 0 {
    for _, group := range xClusterAPIResponse.OutboundReplicationGroups {
      xClusterOutboundResponse := models.XClusterOutboundGroup{
        ReplicationGroupId: group.ReplicationGroupID,
      }

      if group.Metadata.TargetUniverseInfo != nil {
        if group.Metadata.TargetUniverseInfo.UniverseUUID != "" {
          xClusterOutboundResponse.TargetUniverseUuid =
            group.Metadata.TargetUniverseInfo.UniverseUUID
        }
        if group.Metadata.TargetUniverseInfo.State != "" {
          xClusterOutboundResponse.State = group.Metadata.TargetUniverseInfo.State
        }
      } else {
        xClusterOutboundResponse.TargetUniverseUuid = "N/A"
        xClusterOutboundResponse.State = checkpointingState
      }

      tabletServersFuture := make(chan helpers.TabletServersFuture, 1)
      clusterConfigFuture := make(chan helpers.ClusterConfigFuture, 1)
      xClusterTserverLagMetricsFuture := make(chan helpers.XClusterTserverMetricsResponseFuture, 1)

      go c.helper.GetTabletServersFuture(helpers.HOST, tabletServersFuture)
      go c.helper.GetClusterConfigFuture(helpers.HOST, clusterConfigFuture)
      go c.helper.GetXClusterTserverMetricsFuture(helpers.HOST, xClusterTserverLagMetricsFuture)

      tabletServersResponse := <-tabletServersFuture
      clusterConfigResponse := <-clusterConfigFuture

      var nodeList []string = c.helper.GetNodesList(tabletServersResponse)
      xClusterOutboundResponse.ReplicationClusterNodeIps = nodeList
      xClusterOutboundResponse.SourceUniverseUuid = clusterConfigResponse.ClusterConfig.UniverseUuid

      var tablesListWithLag []models.TableReplicationLagDetailsOutbound
      futureTServerMetrics := make(chan helpers.MetricsFuture, 1)

      var sentLagParameter string = "async_replication_sent_lag_micros"
      var committedLagParameter string = "async_replication_committed_lag_micros"

      var urlParam string = fmt.Sprintf("%s,%s", sentLagParameter, committedLagParameter)

      /* Requesting on tServer
        /metrics?metrics=async_replication_sent_lag_micros,async_replication_committed_lag_micros */
      go helpers.GetMetricsFuture(helpers.HOST, helpers.TserverUIPort, urlParam,
        futureTServerMetrics)
      /*
      1. async_replication_sent_lag_micros: This metric represents the time difference between the
         last committed record on the producer (source) and the last record sent to the consumer
         (target). It's available only on the source cluster.
      2. async_replication_committed_lag_micros: This metric represents the time difference between
         the last committed record on the producer (source) and the last applied record on the
         consumer (target). It's also available only on the source cluster.
      */

      tServerMetrics := <-futureTServerMetrics
      if tServerMetrics.Error != nil {
        c.logger.Errorf("[GetMetricsFuture]: %s", tServerMetrics.Error.Error())
        return ctx.String(http.StatusInternalServerError, tServerMetrics.Error.Error())
      }

      // map of (tableID, streamID) => asyncReplicationSentLagMicrosMetrics
      sentLagMap := make(map[string]map[string]int64)
      // map of (tableID, streamID) => asyncReplicationCommittedLagMicrosMetrics
      committedLagMap := make(map[string]map[string]int64)

      tableNamesMap := make(map[string]string)

      for _, item := range *tServerMetrics.MetricsResponse {
        if strings.EqualFold(item.Type, xClusterName) {
          var currentTableID string = item.Attributes.TableID
          var currentStreamID string = item.Attributes.StreamID

          tableNamesMap[currentTableID] = item.Attributes.TableName

          for _, metric := range item.Metrics {
            if metric.Name != committedLagParameter {
              continue
            }
            if sentLagMap[currentTableID] == nil {
              sentLagMap[currentTableID] = make(map[string]int64)
            }
            sentLagMap[currentTableID][currentStreamID] = int64(metric.Value.IntValue)

            if committedLagMap[currentTableID] == nil {
              committedLagMap[currentTableID] = make(map[string]int64)
            }

            committedLagMap[currentTableID][currentStreamID] = metric.Value.IntValue
          }
        }
      }

      for _, nsInfo := range group.Metadata.NamespaceInfos {
        nsName, err := c.GetNamespaceByID(helpers.HOST, nsInfo.Key)
        if err != nil {
          return ctx.String(http.StatusInternalServerError, err.Error())
        }

        tableLag := models.TableReplicationLagDetailsOutbound{Namespace: nsName}

        for _, tableInfo := range nsInfo.Value.TableInfos {
          var tableName string = tableNamesMap[tableInfo.Key]

          if tableName == "" {
            tableName = tableInfo.Key
          }

          var tableKey string = tableInfo.Key
          var tableStreamID string = tableInfo.Value.StreamID
          var sentLag int64 = sentLagMap[tableKey][tableStreamID]
          var committedLag int64 = committedLagMap[tableKey][tableStreamID]

          tableLag.TableUuid = tableInfo.Key
          tableLag.TableName = tableName
          tableLag.IsCheckpointing = tableInfo.Value.IsCheckpointing
          tableLag.IsPartOfInitialBootstrap = tableInfo.Value.IsPartOfInitialBootstrap
          tableLag.AsyncReplicationCommittedLagMicros = committedLag
          tableLag.AsyncReplicationSentLagMicros = sentLag

          tablesListWithLag = append(tablesListWithLag, tableLag)
        }
      }

      xClusterOutboundResponse.TablesListWithLag = tablesListWithLag
      *xClusterResponseOutbound = append(*xClusterResponseOutbound, xClusterOutboundResponse)
    }
  }
  return nil
}

func (c *Container) GetXClusterMetrics(ctx echo.Context) error {

  futureXClusterResponse := make(chan helpers.XClusterFuture)
  futureTables := make(chan helpers.TablesFuture)

  go c.helper.GetXClusterFuture(helpers.HOST, futureXClusterResponse)
  go c.helper.GetTablesFuture(helpers.HOST, true, futureTables)

  var xClusterAPIResponse helpers.XClusterFuture = <-futureXClusterResponse
  var tablesResult helpers.TablesFuture = <-futureTables

  defer close(futureXClusterResponse)
  defer close(futureTables)

  if xClusterAPIResponse.Error != nil {
    c.logger.Errorf("[GetXClusterFuture]: %s", xClusterAPIResponse.Error.Error())
    return ctx.String(http.StatusInternalServerError, xClusterAPIResponse.Error.Error())
  }

  if tablesResult.Error != nil {
    c.logger.Errorf("[GetTablesFuture]: %s", tablesResult.Error.Error())
    return ctx.String(http.StatusInternalServerError, tablesResult.Error.Error())
  }

  outboundErrorChannel := make(chan error)
  inboundErrorChannel := make(chan error)

  var xClusterResponseOutbound []models.XClusterOutboundGroup
  var xClusterResponseInbound []models.XClusterInboundGroup


  /* By using these 2 go-routines instead of 2 standard functions => reduced response
     time from 352ms to 273ms (on average)
     Response time measured using Postman */

  go ProcessOutboundInfoAsync(c, xClusterAPIResponse.XClusterResponse, ctx,
    &xClusterResponseOutbound, outboundErrorChannel)
  go ProcessInboundInfoAsync(c, xClusterAPIResponse.XClusterResponse, ctx,
    &xClusterResponseInbound, inboundErrorChannel)

  var outboundError error = <-outboundErrorChannel
  var inboundError error = <-inboundErrorChannel

  defer close(outboundErrorChannel)
  defer close(inboundErrorChannel)

  if outboundError != nil {
    return outboundError
  }

  if inboundError != nil {
    return inboundError
  }

  var xClusterFinalResponse models.XClusterReplicationGroups

  xClusterFinalResponse.InboundReplicationGroups = xClusterResponseInbound
  xClusterFinalResponse.OutboundReplicationGroups = xClusterResponseOutbound

  return ctx.JSON(ctx.Response().Status, xClusterFinalResponse)
}

func (c *Container) GetXClusterSafeTime(ctx echo.Context) error {
  futureXCluster := make(chan helpers.XClusterFuture)
  go c.helper.GetXClusterFuture(helpers.HOST, futureXCluster)
  result := <-futureXCluster
  close(futureXCluster)
  var isClusterInbound bool = false
  if len(result.XClusterResponse.ConsumerRegistry.ProducerMap) > 0 {
    isClusterInbound = true
  }
  if isClusterInbound == false {
    return ctx.JSON(403, map[string]interface{}{
      "message": "We do not produce safe time metrics for non-inbound clusters",
    })
  }
  mastersFuture := make(chan helpers.MastersFuture)
  go c.helper.GetMastersFuture(mastersFuture)
  mastersResponse := <-mastersFuture
  close(mastersFuture)
  if mastersResponse.Error != nil {
    c.logger.Errorf("[GetMastersFuture]: %s", mastersResponse.Error.Error())
    return ctx.String(http.StatusInternalServerError, mastersResponse.Error.Error())
  }

  var masterAddressesCommaSeparated string
  for _, master := range mastersResponse.Masters {
    if len(master.Registration.PrivateRpcAddresses) > 0 {
      masterAddressesCommaSeparated += fmt.Sprintf(
        "%s:%d,",
        master.Registration.PrivateRpcAddresses[0].Host,
        master.Registration.PrivateRpcAddresses[0].Port)
    }
  }
  if masterAddressesCommaSeparated[len(masterAddressesCommaSeparated)-1:] == "," {
    masterAddressesCommaSeparated = strings.TrimRight(masterAddressesCommaSeparated, ",")
  }
  params := []string{
    "--master_addresses",
    masterAddressesCommaSeparated,
    "get_xcluster_safe_time",
    "include_lag_and_skew",
  }

  safeTimeFuture := make(chan helpers.YBAdminFuture)
  go c.helper.RunYBAdminFuture(params, safeTimeFuture)
  safeTimeResult := <-safeTimeFuture

  if safeTimeResult.Error != nil {
    c.logger.Errorf("failed to execute get_xcluster_safe_time: %s", safeTimeResult.Error.Error())
    return ctx.String(http.StatusInternalServerError, safeTimeResult.Error.Error())
  }

  var safeTimeData []XClusterSafeTime
  err := json.Unmarshal([]byte(safeTimeResult.Result), &safeTimeData)
  if err != nil {
    c.logger.Errorf("failed to parse get_xcluster_safe_time response: %s", err.Error())
    return ctx.String(http.StatusInternalServerError, "Failed to parse response from yb-admin")
  }
  return ctx.JSON(http.StatusOK, safeTimeData)
}
