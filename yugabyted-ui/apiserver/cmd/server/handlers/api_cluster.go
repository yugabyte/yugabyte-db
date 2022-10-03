package handlers

import (
    "apiserver/cmd/server/helpers"
    "apiserver/cmd/server/models"
    "encoding/json"
    "fmt"
    "net/http"
    "sort"
    "time"

    "github.com/labstack/echo/v4"
    "github.com/yugabyte/gocql"
)

const QUERY_LIMIT_ONE string = "select ts, value, details " +
    "from %s where metric='%s' and node='%s' limit 1;"

// CreateCluster - Create a cluster
func (c *Container) CreateCluster(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld{
        Message: "Hello World",
    })
}

// DeleteCluster - Submit task to delete a cluster
func (c *Container) DeleteCluster(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld{
        Message: "Hello World",
    })
}

// EditCluster - Submit task to edit a cluster
func (c *Container) EditCluster(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld{
        Message: "Hello World",
    })
}

// GetCluster - Get a cluster
func (c *Container) GetCluster(ctx echo.Context) error {
    // Perform all necessary http requests asynchronously
    tabletServersFuture := make(chan helpers.TabletServersFuture)
    mastersFuture := make(chan helpers.MastersFuture)
    clusterConfigFuture := make(chan helpers.ClusterConfigFuture)
    go helpers.GetTabletServersFuture(helpers.HOST, tabletServersFuture)
    go helpers.GetMastersFuture(helpers.HOST, mastersFuture)
    go helpers.GetClusterConfigFuture(helpers.HOST, clusterConfigFuture)

    // Get response from tabletServersFuture
    tabletServersResponse := <-tabletServersFuture
    if tabletServersResponse.Error != nil {
        return ctx.String(http.StatusInternalServerError, tabletServersResponse.Error.Error())
    }

    // Now that we have tabletServersResponse, we can start doing queries that need to be made to
    // each node separately
    // - Getting gflags for each tserver/master
    // - Getting version information from each node
    nodeList := helpers.GetNodesList(tabletServersResponse)
    gFlagsTserverFutures := []chan helpers.GFlagsFuture{}
    gFlagsMasterFutures := []chan helpers.GFlagsFuture{}
    versionInfoFutures := []chan helpers.VersionInfoFuture{}
    for _, nodeHost := range nodeList {
        gFlagsTserverFuture := make(chan helpers.GFlagsFuture)
        gFlagsTserverFutures = append(gFlagsTserverFutures, gFlagsTserverFuture)
        go helpers.GetGFlagsFuture(nodeHost, false, gFlagsTserverFuture)
        gFlagsMasterFuture := make(chan helpers.GFlagsFuture)
        gFlagsMasterFutures = append(gFlagsMasterFutures, gFlagsMasterFuture)
        go helpers.GetGFlagsFuture(nodeHost, true, gFlagsTserverFuture)
        versionInfoFuture := make(chan helpers.VersionInfoFuture)
        versionInfoFutures = append(versionInfoFutures, versionInfoFuture)
        go helpers.GetVersionFuture(nodeHost, versionInfoFuture)
    }

    // Getting relevant data from tabletServersResponse
    regionsMap := map[string]int32{}
    zonesMap := map[string]int32{}
    numNodes := int32(0)
    totalDisk := uint64(0)
    ramUsageBytes := float64(0)
    for _, cluster := range tabletServersResponse.Tablets {
        for _, tablet := range cluster {
            numNodes++;
            region := tablet.Region
            regionsMap[region]++
            zone := tablet.Zone
            zonesMap[zone]++
            ramUsageBytes += float64(tablet.RamUsedBytes)
            for _, pathMetric := range tablet.PathMetrics {
                totalSpaceSize := pathMetric.TotalSpaceSize
                if totalDisk < totalSpaceSize {
                    totalDisk = totalSpaceSize
                }
            }
        }
    }
    // convert from bytes to MB
    ramUsageMb := ramUsageBytes / helpers.BYTES_IN_MB
    // convert from bytes to GB
    totalDiskGb := int32(totalDisk / helpers.BYTES_IN_GB)
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
            // assumes there cannot be two zones with the same name but in different regions
            faultTolerance = models.CLUSTERFAULTTOLERANCE_ZONE
        } else {
            faultTolerance = models.CLUSTERFAULTTOLERANCE_NODE
        }
    }
    // Determine if encryption at rest is enabled
    // Checks cluster-config response encryption_info.encryption_enabled
    clusterConfigResponse := <-clusterConfigFuture
    isEncryptionAtRestEnabled := false
    if clusterConfigResponse.Error == nil {
        resultConfig := clusterConfigResponse.ClusterConfig
        isEncryptionAtRestEnabled = resultConfig.EncryptionInfo.EncryptionEnabled
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
            }
        }
    }
    // to get cpu usage, need to query each node we run the query
    // 'select * from system.metrics where metric='cpu_usage_user' and node='node_uuid' limit 1;'
    // this gets the newest stat for one node. Do for all nodes and combine.
    // do the same with with cpu_usage_system as well.
    cluster := gocql.NewCluster(helpers.HOST)

    // Use the same timeout as the Java driver.
    cluster.Timeout = 12 * time.Second

    // Create the session.
    session, err := cluster.CreateSession()
    averageCpu := float64(0)
    if err == nil {
        defer session.Close()
        hostToUuid, err := helpers.GetHostToUuidMap(helpers.HOST)
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
                    continue
                }
                query = fmt.Sprintf(QUERY_LIMIT_ONE, "system.metrics", "cpu_usage_system", uuid)
                iter = session.Query(query).Iter()
                iter.Scan(&ts, &value, &details)
                json.Unmarshal([]byte(details), &detailObj)
                sum += detailObj.Value
                if err := iter.Close(); err != nil {
                    continue
                }
            }
            averageCpu = (sum * 100) / float64(len(hostToUuid))
        }
    }

    // Get software version
    smallestVersion := ""
    for _, versionInfoFuture := range versionInfoFutures {
        versionInfo := <-versionInfoFuture
        if versionInfo.Error == nil {
            versionNumber := versionInfo.VersionInfo.VersionNumber
            if smallestVersion == "" ||
               helpers.CompareVersions(smallestVersion, versionNumber) > 0 {
                smallestVersion = versionNumber
            }
        }
    }

    response := models.ClusterResponse{
        Data: models.ClusterData{
            Spec: models.ClusterSpec{
                CloudInfo: models.CloudInfo{
                    Code: provider,
                },
                ClusterInfo: models.ClusterInfo{
                    NumNodes:       numNodes,
                    FaultTolerance: faultTolerance,
                    NodeInfo: models.ClusterNodeInfo{
                        MemoryMb:   ramUsageMb,
                        DiskSizeGb: totalDiskGb,
                        CpuUsage:   averageCpu,
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

// ListClusters - List clusters
func (c *Container) ListClusters(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld{
        Message: "Hello World",
    })
}

// NodeOp - Submit task to operate on a node
func (c *Container) NodeOp(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld{
        Message: "Hello World",
    })
}

// PauseCluster - Submit task to pause a cluster
func (c *Container) PauseCluster(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld{
        Message: "Hello World",
    })
}

// ResumeCluster - Submit task to resume a cluster
func (c *Container) ResumeCluster(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld{
        Message: "Hello World",
    })
}
