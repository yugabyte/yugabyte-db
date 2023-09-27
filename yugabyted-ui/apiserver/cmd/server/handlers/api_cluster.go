package handlers

import (
    "apiserver/cmd/server/helpers"
    "apiserver/cmd/server/logger"
    "apiserver/cmd/server/models"
    "encoding/json"
    "fmt"
    "net/http"
    "runtime"
    "sort"
    "time"

    "github.com/labstack/echo/v4"
)

const QUERY_LIMIT_ONE string = "select ts, value, details " +
        "from %s where metric='%s' and node='%s' limit 1;"

// GetCluster - Get a cluster
func (c *Container) GetCluster(ctx echo.Context) error {
        // Perform all necessary http requests asynchronously
        tabletServersFuture := make(chan helpers.TabletServersFuture)
        mastersFuture := make(chan helpers.MastersFuture)
        clusterConfigFuture := make(chan helpers.ClusterConfigFuture)
        go helpers.GetTabletServersFuture(c.logger, helpers.HOST, tabletServersFuture)
        go helpers.GetMastersFuture(helpers.HOST, mastersFuture)
        go helpers.GetClusterConfigFuture(helpers.HOST, clusterConfigFuture)

        // Get response from tabletServersFuture
        tabletServersResponse := <-tabletServersFuture
        if tabletServersResponse.Error != nil {
                return ctx.String(http.StatusInternalServerError,
                        tabletServersResponse.Error.Error())
        } else {
            logger.Log.Errorf("[tabletServersResponse]", tabletServersResponse.Error)
        }

        // Now that we have tabletServersResponse, we can start doing
        // queries that need to be made to each node separately
        // - Getting gflags for each tserver/master
        // - Getting version information from each node
        // - Getting ram limits from each tserver/master
        nodeList := helpers.GetNodesList(tabletServersResponse)
        reducedNodeList := helpers.RemoveLocalAddresses(nodeList)
        gFlagsTserverFutures := []chan helpers.GFlagsFuture{}
        gFlagsMasterFutures := []chan helpers.GFlagsFuture{}
        versionInfoFutures := []chan helpers.VersionInfoFuture{}
        masterMemTrackersFutures := []chan helpers.MemTrackersFuture{}
        tserverMemTrackersFutures := []chan helpers.MemTrackersFuture{}
        for _, nodeHost := range nodeList {
                gFlagsTserverFuture := make(chan helpers.GFlagsFuture)
                gFlagsTserverFutures = append(gFlagsTserverFutures, gFlagsTserverFuture)
                go helpers.GetGFlagsFuture(nodeHost, false, gFlagsTserverFuture)
                gFlagsMasterFuture := make(chan helpers.GFlagsFuture)
                gFlagsMasterFutures = append(gFlagsMasterFutures, gFlagsMasterFuture)
                go helpers.GetGFlagsFuture(nodeHost, true, gFlagsMasterFuture)
                versionInfoFuture := make(chan helpers.VersionInfoFuture)
                versionInfoFutures = append(versionInfoFutures, versionInfoFuture)
                go helpers.GetVersionFuture(nodeHost, versionInfoFuture)
        }
        for _, nodeHost := range reducedNodeList {
                masterMemTrackersFuture := make(chan helpers.MemTrackersFuture)
                masterMemTrackersFutures = append(masterMemTrackersFutures, masterMemTrackersFuture)
                go helpers.GetMemTrackersFuture(nodeHost, true, masterMemTrackersFuture)
                tserverMemTrackersFuture := make(chan helpers.MemTrackersFuture)
                tserverMemTrackersFutures =
                    append(tserverMemTrackersFutures, tserverMemTrackersFuture)
                go helpers.GetMemTrackersFuture(nodeHost, false, tserverMemTrackersFuture)
        }

    // Getting relevant data from tabletServersResponse
    regionsMap := map[string]int32{}
    zonesMap := map[string]int32{}
    numNodes := int32(0)
    ramUsageBytes := float64(0)
    for _, cluster := range tabletServersResponse.Tablets {
        for _, tablet := range cluster {
            numNodes++;
            region := tablet.Region
            regionsMap[region]++
            zone := tablet.Zone
            zonesMap[zone]++
            ramUsageBytes += float64(tablet.RamUsedBytes)
        }
    }
    // convert from bytes to MB
    ramUsageMb := ramUsageBytes / helpers.BYTES_IN_MB
    // convert from bytes to GB
    provider := models.CLOUDENUM_MANUAL
    clusterRegionInfo := []models.ClusterRegionInfo{}
    for region, numNodesInRegion := range regionsMap {
        clusterRegionInfo = append(clusterRegionInfo, models.ClusterRegionInfo{
            PlacementInfo: models.PlacementInfo{
                CloudInfo: models.CloudInfo{
                    Code:   provider,
                    Region: region,
                },
                NumNodes: numNodesInRegion,
            },
        })
    }
    sort.Slice(clusterRegionInfo, func(i, j int) bool {
        return clusterRegionInfo[i].PlacementInfo.CloudInfo.Region <
               clusterRegionInfo[j].PlacementInfo.CloudInfo.Region
    })

        // Getting response from mastersFuture
        mastersResponse := <-mastersFuture
        if mastersResponse.Error != nil {
                return ctx.String(http.StatusInternalServerError, mastersResponse.Error.Error())
        }

        // Getting relevant data from mastersResponse
        timestamp := time.Now().UnixMicro()
        for _, master := range mastersResponse.Masters {
                startTime := master.InstanceId.StartTimeUs
                if startTime < timestamp && startTime != 0 {
                        timestamp = startTime
                }
        }
        createdOn := time.UnixMicro(timestamp).Format(time.RFC3339)
        // Less than 3 replicas -> None
        // In at least 3 different regions -> Region
        // In at least 3 different zones but fewer than 3 regions -> Zone
        // At least 3 replicas but in fewer than 3 zones -> Node
        faultTolerance := models.CLUSTERFAULTTOLERANCE_NONE
        if numNodes >= 3 {
                if len(regionsMap) >= 3 {
                        // regionsMap comes from parsing /tablet-servers endpoint
                        faultTolerance = models.CLUSTERFAULTTOLERANCE_REGION
                } else if len(zonesMap) >= 3 {
                        // zonesMap comes from parsing /tablet-servers endpoint
                        // assumes there cannot be two zones with the same name
                        // but in different regions
                        faultTolerance = models.CLUSTERFAULTTOLERANCE_ZONE
                } else {
                        faultTolerance = models.CLUSTERFAULTTOLERANCE_NODE
                }
        }
        // Determine if encryption at rest is enabled
        // Checks cluster-config response encryption_info.encryption_enabled
        clusterConfigResponse := <-clusterConfigFuture
        isEncryptionAtRestEnabled := false
        var clusterReplicationFactor int32
        if clusterConfigResponse.Error == nil {
                resultConfig := clusterConfigResponse.ClusterConfig
                isEncryptionAtRestEnabled = resultConfig.EncryptionInfo.EncryptionEnabled
                clusterReplicationFactor = int32(resultConfig.ReplicationInfo.
                                                   LiveReplicas.NumReplicas)
        }
        // Determine if encryption in transit is enabled
        // It is enabled if and only if each master and tserver has the flags:
        //   --use_node_to_node_encryption=true
        //   --allow_insecure_connections=false
        // and each tserver has the flag:
        //   --use_client_to_server_encryption=true
        // If any flag on any server does not match, we don't say encryption in transit is enabled.
        isEncryptionInTransitEnabled := true
        for _, gFlagsTserverFuture := range gFlagsTserverFutures {
                tserverFlags := <-gFlagsTserverFuture
                if tserverFlags.Error != nil ||
                        tserverFlags.GFlags["use_node_to_node_encryption"] != "true" ||
                        tserverFlags.GFlags["allow_insecure_connections"] != "false" ||
                        tserverFlags.GFlags["use_client_to_server_encryption"] != "true" {
                        isEncryptionInTransitEnabled = false
                        break
                } else {
                    logger.Log.Errorf("[tserverFlags]", tserverFlags.Error)
                }
        }
        // Only need to keep checking masters if it is still possible that in-transit encryption is
        // enabled.
        if isEncryptionInTransitEnabled {
                for _, gFlagsMasterFuture := range gFlagsMasterFutures {
                        masterFlags := <-gFlagsMasterFuture
                        if masterFlags.Error != nil ||
                                masterFlags.GFlags["use_node_to_node_encryption"] != "true" ||
                                masterFlags.GFlags["allow_insecure_connections"] != "false" {
                                isEncryptionInTransitEnabled = false
                                break
                        } else {
                            logger.Log.Errorf("[masterFlags]", masterFlags.Error)
                        }
                }
        }

        // Use the session from the context.
        session, err := c.GetSession()
        if err != nil {
            return ctx.String(http.StatusInternalServerError, err.Error())
        }
        averageCpu := float64(0)
        totalDiskGb := float64(0)
        freeDiskGb := float64(0)
        hostToUuid, err := helpers.GetHostToUuidMap(c.logger, helpers.HOST)
        if err == nil {
            sum := float64(0)
            for _, uuid := range hostToUuid {
                query := fmt.Sprintf(QUERY_LIMIT_ONE, "system.metrics", "cpu_usage_user", uuid)
                iter := session.Query(query).Iter()
                var ts int64
                var value int
                var details string
                iter.Scan(&ts, &value, &details)
                detailObj := DetailObj{}
                json.Unmarshal([]byte(details), &detailObj)
                sum += detailObj.Value
                if err := iter.Close(); err != nil {
                    logger.Log.Errorf("[api_cluster] error fetching cpu_usage_user data", err)
                    continue
                }
                query = fmt.Sprintf(QUERY_LIMIT_ONE, "system.metrics", "cpu_usage_system", uuid)
                iter = session.Query(query).Iter()
                iter.Scan(&ts, &value, &details)
                json.Unmarshal([]byte(details), &detailObj)
                sum += detailObj.Value
                if err := iter.Close(); err != nil {
                    logger.Log.Errorf("[api_cluster] error fetching cpu_usage_system data", err)
                    continue
                }
            }
            averageCpu = (sum * 100) / float64(len(hostToUuid))

            // Get the disk usage as well. We assume all nodes report the size of a different disk,
            // unless the node has a 127.x.x.x address, which assume reports the disk size of the
            // same machine. If all addresses are 127.x.x.x addresses, assume there is only
            // one machine in the cluster. If there are a mix of 127.x.x.x and non 127.x.x.x
            // addresses, assume the 127.x.x.x addresses share a machine with one of the non
            // 127.x.x.x addresses.

            // Thus, we will only count the metrics from non 127 addresses, unless all addresses
            // are 127, in which case we count only one of them.
            for _, host := range reducedNodeList {
                query := fmt.Sprintf(
                    QUERY_LIMIT_ONE, "system.metrics", "total_disk", hostToUuid[host])
                iter := session.Query(query).Iter()
                var ts int64
                var value int
                var details string
                iter.Scan(&ts, &value, &details)
                totalDiskGb += float64(value) / helpers.BYTES_IN_GB
                query = fmt.Sprintf(
                    QUERY_LIMIT_ONE, "system.metrics", "free_disk", hostToUuid[host])
                iter = session.Query(query).Iter()
                iter.Scan(&ts, &value, &details)
                freeDiskGb += float64(value) / helpers.BYTES_IN_GB
            }
        } else{
            logger.Log.Errorf("[GetHostToUuidMap]", err)
        }
        // Get software version
        smallestVersion := helpers.GetSmallestVersion(versionInfoFutures)
        numCores := numNodes * int32(runtime.NumCPU())

        // Get ram limits
        ramProvisionedBytes := float64(0)
        for _, memTrackersFuture :=
            range append(tserverMemTrackersFutures, masterMemTrackersFutures...) {
            memTrackersResult := <-memTrackersFuture
            if memTrackersResult.Error == nil {
                ramProvisionedBytes += float64(memTrackersResult.Limit)
            }
        }
        ramProvisionedGb := ramProvisionedBytes / helpers.BYTES_IN_GB

    response := models.ClusterResponse{
        Data: models.ClusterData{
            Spec: models.ClusterSpec{
                CloudInfo: models.CloudInfo{
                    Code: provider,
                },
                ClusterInfo: models.ClusterInfo{
                    NumNodes:       numNodes,
                    FaultTolerance: faultTolerance,
                    ReplicationFactor: clusterReplicationFactor,
                    NodeInfo: models.ClusterNodeInfo{
                        MemoryMb:         ramUsageMb,
                        DiskSizeGb:       totalDiskGb,
                        DiskSizeUsedGb:   totalDiskGb - freeDiskGb,
                        CpuUsage:         averageCpu,
                        NumCores:         numCores,
                        RamProvisionedGb: ramProvisionedGb,
                    },
                },
                ClusterRegionInfo: &clusterRegionInfo,
                EncryptionInfo: models.EncryptionInfo{
                    EncryptionAtRest:    isEncryptionAtRestEnabled,
                    EncryptionInTransit: isEncryptionInTransitEnabled,
                },
            },
            Info: models.ClusterDataInfo{
                Metadata: models.EntityMetadata{
                    CreatedOn: &createdOn,
                    UpdatedOn: &createdOn,
                },
                SoftwareVersion: smallestVersion,
            },
        },
    }
    return ctx.JSON(http.StatusOK, response)
}
