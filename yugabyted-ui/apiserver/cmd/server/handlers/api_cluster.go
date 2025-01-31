package handlers

import (
    "apiserver/cmd/server/helpers"
    "apiserver/cmd/server/models"
    "encoding/json"
    "fmt"
    "net/http"
    "runtime"
    "sort"
    "strconv"
    "time"

    "github.com/labstack/echo/v4"
)
const MOST_RECENT_TIMESTAMP string = "select ts " +
        "from %s where metric='%s' and node='%s' limit 1;"
const QUERY_LIMIT_ONE string = "select ts, value, details " +
        "from %s where metric='%s' and node='%s' limit 1;"
const MRT_QUERY_METRICS string = "select ts, value, details " +
        "from %s where metric='%s' and node='%s' and ts>=%d"
// GetCluster - Get a cluster
func (c *Container) GetCluster(ctx echo.Context) error {
        // Perform all necessary http requests asynchronously
        tabletServersFuture := make(chan helpers.TabletServersFuture)
        mastersFuture := make(chan helpers.MastersFuture)
        clusterConfigFuture := make(chan helpers.ClusterConfigFuture)
        masterAddressesFuture := make(chan helpers.MasterAddressesFuture)
        go c.helper.GetTabletServersFuture(helpers.HOST, tabletServersFuture)
        go c.helper.GetMastersFuture(mastersFuture)
        go c.helper.GetClusterConfigFuture(helpers.HOST, clusterConfigFuture)
        go c.helper.GetMasterAddressesFuture(masterAddressesFuture)

        // Get response from tabletServersFuture
        tabletServersResponse := <-tabletServersFuture
        if tabletServersResponse.Error != nil {
                c.logger.Errorf("[tabletServersResponse]: %s", tabletServersResponse.Error.Error())
                return ctx.String(http.StatusInternalServerError,
                        tabletServersResponse.Error.Error())
        }

        masterAddressesResponse := <-masterAddressesFuture
        if masterAddressesResponse.Error != nil {
            c.logger.Errorf("[masterAddressesResponse]: %s", masterAddressesResponse.Error.Error())
            return ctx.String(http.StatusInternalServerError, masterAddressesResponse.Error.Error())
        }

        // Now that we have tabletServersResponse, we can start doing
        // queries that need to be made to each node separately
        // - Getting gflags for each tserver/master
        // - Getting version information from each node
        // - Getting ram limits from each tserver/master
        nodeList := c.helper.GetNodesList(tabletServersResponse)
        reducedNodeList := c.helper.RemoveLocalAddresses(nodeList)
        reducedMasterList := c.helper.RemoveLocalAddresses(masterAddressesResponse.HostList)
        gFlagsTserverFutures := map[string]chan helpers.GFlagsFuture{}
        gFlagsMasterFutures := map[string]chan helpers.GFlagsFuture{}
        versionInfoFutures := map[string]chan helpers.VersionInfoFuture{}
        versionInfoMasterFutures := map[string]chan helpers.VersionInfoFuture{}
        masterMemTrackersFutures := map[string]chan helpers.MemTrackersFuture{}
        tserverMemTrackersFutures := map[string]chan helpers.MemTrackersFuture{}
        for _, nodeHost := range nodeList {
            gFlagsTserverFuture := make(chan helpers.GFlagsFuture)
            gFlagsTserverFutures[nodeHost] = gFlagsTserverFuture
            go c.helper.GetGFlagsFuture(nodeHost, false, gFlagsTserverFuture)
            versionInfoFuture := make(chan helpers.VersionInfoFuture)
            versionInfoFutures[nodeHost] = versionInfoFuture
            go c.helper.GetVersionFuture(nodeHost, false, versionInfoFuture)
        }
        for _, nodeHost := range masterAddressesResponse.HostList {
            gFlagsMasterFuture := make(chan helpers.GFlagsFuture)
            gFlagsMasterFutures[nodeHost] = gFlagsMasterFuture
            go c.helper.GetGFlagsFuture(nodeHost, true, gFlagsMasterFuture)
            versionInfoFuture := make(chan helpers.VersionInfoFuture)
            versionInfoMasterFutures[nodeHost] = versionInfoFuture
            go c.helper.GetVersionFuture(nodeHost, true, versionInfoFuture)
        }
        for _, nodeHost := range reducedNodeList {
            tserverMemTrackersFuture := make(chan helpers.MemTrackersFuture)
            tserverMemTrackersFutures[nodeHost] = tserverMemTrackersFuture
            go c.helper.GetMemTrackersFuture(nodeHost, false, tserverMemTrackersFuture)
        }
        for _, nodeHost := range reducedMasterList {
            masterMemTrackersFuture := make(chan helpers.MemTrackersFuture)
            masterMemTrackersFutures[nodeHost] = masterMemTrackersFuture
            go c.helper.GetMemTrackersFuture(nodeHost, true, masterMemTrackersFuture)
        }

    // Getting relevant data from tabletServersResponse
    regionsMap := map[string]int32{}
    zonesMap := map[string]int32{}
    tserverMap := map[string]bool{}
    ramUsageBytes := float64(0)
    for _, cluster := range tabletServersResponse.Tablets {
        for host, tablet := range cluster {
            tserverMap[host] = true
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
            c.logger.Errorf("[mastersResponse]: %s", mastersResponse.Error.Error())
            return ctx.String(http.StatusInternalServerError, mastersResponse.Error.Error())
        }

        // Getting relevant data from mastersResponse
        timestamp := time.Now().UnixMicro()
        numMasters := int32(0)
        for _, master := range mastersResponse.Masters {
                startTime := master.InstanceId.StartTimeUs
                if startTime < timestamp && startTime != 0 {
                        timestamp = startTime
                }
                if len(master.Registration.PrivateRpcAddresses) > 0 {
                    if _, ok := tserverMap[master.Registration.PrivateRpcAddresses[0].Host]; !ok {
                        numMasters++
                    }
                }
        }
        createdOn := time.UnixMicro(timestamp).Format(time.RFC3339)
        // Less than 3 replicas -> None
        // In at least 3 different regions -> Region
        // In at least 3 different zones but fewer than 3 regions -> Zone
        // At least 3 replicas but in fewer than 3 zones -> Node
        faultTolerance := models.CLUSTERFAULTTOLERANCE_NONE
        numNodes := int32(len(tserverMap)) // + numMasters
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
        } else {
            c.logger.Warnf("[clusterConfigResponse]: %s", clusterConfigResponse.Error.Error())
        }
        // Determine if encryption in transit is enabled
        // It is enabled if and only if each master and tserver has the flags:
        //   --use_node_to_node_encryption=true
        //   --allow_insecure_connections=false
        // and each tserver has the flag:
        //   --use_client_to_server_encryption=true
        // If any flag on any server does not match, we don't say encryption in transit is enabled.
        tServerSnapshotMap := make(map[string]string)
        isEncryptionInTransitEnabled := true
        for host, gFlagsTserverFuture := range gFlagsTserverFutures {
            tserverFlags := <-gFlagsTserverFuture
            if tserverFlags.Error != nil ||
               tserverFlags.GFlags["use_node_to_node_encryption"] != "true" ||
               tserverFlags.GFlags["allow_insecure_connections"] != "false" ||
               tserverFlags.GFlags["use_client_to_server_encryption"] != "true" {
                    isEncryptionInTransitEnabled = false
                    c.logger.Debugf("encryption in transit not enabled at tserver %s", host)
                    if tserverFlags.Error != nil {
                        c.logger.Debugf("tserverFlags error: %s", tserverFlags.Error.Error())
                    } else {
                        c.logger.Debugf("tserverFlags use_node_to_node_encryption: %s",
                            tserverFlags.GFlags["use_node_to_node_encryption"])
                        c.logger.Debugf("tserverFlags allow_insecure_connections: %s",
                            tserverFlags.GFlags["allow_insecure_connections"])
                        c.logger.Debugf("tserverFlags use_client_to_server_encryption: %s",
                            tserverFlags.GFlags["use_client_to_server_encryption"])
                    }
                    break
            }
            tServerSnapshotMap[host] = tserverFlags.GFlags["metrics_snapshotter_interval_ms"]
        }
        // Use this to track gflag results that have been read, to avoid reading a future twice
        // which will hang
        gFlagsMasterResults := map[string]helpers.GFlagsFuture{}
        // Only need to keep checking masters if it is still possible that in-transit encryption is
        // enabled.
        if isEncryptionInTransitEnabled {
            for host, gFlagsMasterFuture := range gFlagsMasterFutures {
                if _, ok := gFlagsMasterResults[host]; !ok {
                    gFlagsMasterResults[host] = <-gFlagsMasterFuture
                }
                masterFlags := gFlagsMasterResults[host]
                if masterFlags.Error != nil ||
                   masterFlags.GFlags["use_node_to_node_encryption"] != "true" ||
                   masterFlags.GFlags["allow_insecure_connections"] != "false" {
                        isEncryptionInTransitEnabled = false
                        c.logger.Debugf("encryption in transit not enabled because at master %s",
                            host)
                        if masterFlags.Error != nil {
                            c.logger.Debugf("masterFlags error: %s", masterFlags.Error.Error())
                        } else {
                            c.logger.Debugf("masterFlags use_node_to_node_encryption: %s",
                                masterFlags.GFlags["use_node_to_node_encryption"])
                            c.logger.Debugf("masterFlags allow_insecure_connections: %s",
                                masterFlags.GFlags["allow_insecure_connections"])
                        }
                        break
                }
            }
        }

        // We can get clusterReplicationFactor == 0 if /cluster-config doesn't return the
        // livereplicas structure. In this case, use the master gflag to get replication factor
        if clusterReplicationFactor == 0 {
            for host, gFlagsMasterFuture := range gFlagsMasterFutures {
                if _, ok := gFlagsMasterResults[host]; !ok {
                    gFlagsMasterResults[host] = <-gFlagsMasterFuture
                }
                masterFlags := gFlagsMasterResults[host]
                if masterFlags.Error == nil {
                    gflagReplicationFactor, err :=
                        strconv.ParseInt(masterFlags.GFlags["replication_factor"], 10, 32)
                    if err == nil && gflagReplicationFactor > 0 {
                        clusterReplicationFactor = int32(gflagReplicationFactor)
                        break
                    }
                    if err != nil {
                        c.logger.Warnf("error parsing replication_factor gflag: %s", err.Error())
                    } else {
                        c.logger.Warnf("replication_factor gflag is 0")
                    }
                }
            }
        }

        // Use the session from the context.
        session, err := c.GetSession()
        if err != nil {
            c.logger.Errorf("[GetSession]: %s", err.Error())
            return ctx.String(http.StatusInternalServerError, err.Error())
        }
        averageCpu := float64(0)
        totalDiskGb := float64(0)
        freeDiskGb := float64(0)
        hostToUuid := c.helper.GetHostToUuidMapFromFuture(tabletServersResponse)
        sum := float64(0)
        errorCount := 0
        for host, uuid := range hostToUuid {
            query := fmt.Sprintf(QUERY_LIMIT_ONE, "system.metrics", "cpu_usage_user", uuid)
            iter := session.Query(query).Iter()
            var ts int64
            var value int
            var details string
            iter.Scan(&ts, &value, &details)
            detailObj := DetailObj{}
            json.Unmarshal([]byte(details), &detailObj)
            if err := iter.Close(); err != nil {
                errorCount++
                c.logger.Errorf("error fetching cpu_usage_user data from %s: %s",
                    host, err.Error())
                continue
            } else {
                sum += detailObj.Value
            }
            query = fmt.Sprintf(QUERY_LIMIT_ONE, "system.metrics", "cpu_usage_system", uuid)
            iter = session.Query(query).Iter()
            iter.Scan(&ts, &value, &details)
            json.Unmarshal([]byte(details), &detailObj)
            if err := iter.Close(); err != nil {
                errorCount++
                c.logger.Errorf("error fetching cpu_usage_system data from %s: %s",
                    host, err.Error())
                continue
            } else {
                sum += detailObj.Value
            }
        }
        // subtract errorCount to only take average of nodes that are up
        averageCpu = (sum * 100) / float64(len(hostToUuid) - errorCount)

        // Get the disk usage as well. We assume all nodes report the size of a different disk,
        // unless the node has a 127.x.x.x address, which assume reports the disk size of the
        // same machine. If all addresses are 127.x.x.x addresses, assume there is only
        // one machine in the cluster. If there are a mix of 127.x.x.x and non 127.x.x.x
        // addresses, assume the 127.x.x.x addresses share a machine with one of the non
        // 127.x.x.x addresses.

        // Thus, we will only count the metrics from non 127 addresses, unless all addresses
        // are 127, in which case we count only one of them.
        for _, host := range reducedNodeList {
            var recentTs int64
            var snapshotterInterval int64
            var errDataType error
            var refinedTs int64
            snapshotterIntervalStr := tServerSnapshotMap[host]
            if snapshotterIntervalStr == "" {
                snapshotterIntervalStr = "11000" //default value is set to 11000ms
            }
            snapshotterInterval, errDataType = strconv.ParseInt(snapshotterIntervalStr, 10, 64)
            if errDataType != nil {
                fmt.Println("Error: metrics_snapshotter_interval_ms not in proper type")
            }
            timestamp := fmt.Sprintf(
                MOST_RECENT_TIMESTAMP, "system.metrics", "total_disk", hostToUuid[host])
            iter := session.Query(timestamp).Iter()
            iter.Scan(&recentTs)
            refinedTs = recentTs - snapshotterInterval / 2
            query := fmt.Sprintf(
                MRT_QUERY_METRICS, "system.metrics", "total_disk", hostToUuid[host], refinedTs)
            iter = session.Query(query).Iter()
            var ts int64
            var value int
            var details string
            for iter.Scan(&ts, &value, &details) {
                totalDiskGb += float64(value) / helpers.BYTES_IN_GB
            }
            if err := iter.Close(); err != nil {
                c.logger.Errorf("error fetching total_disk data from %s: %s",
                    host, err.Error())
                continue
            }
            query = fmt.Sprintf(
                MRT_QUERY_METRICS, "system.metrics", "free_disk", hostToUuid[host], refinedTs)
            refinedTs = snapshotterInterval / 2
            iter = session.Query(query).Iter()
            for iter.Scan(&ts, &value, &details) {
                freeDiskGb += float64(value) / helpers.BYTES_IN_GB
            }
            if err := iter.Close(); err != nil {
                c.logger.Errorf("error fetching free_disk data from %s: %s",
                    host, err.Error())
                continue
            }
        }
        // Get software version
        smallestVersion := c.helper.GetSmallestVersion(versionInfoFutures)
        smallestVersionMaster := c.helper.GetSmallestVersion(versionInfoMasterFutures)
        if smallestVersion == "" ||
            c.helper.CompareVersions(smallestVersion, smallestVersionMaster) > 0 {
            smallestVersion = smallestVersionMaster
        }
        numCores := int32(len(reducedNodeList)) * int32(runtime.NumCPU())

        // Get ram limits
        ramProvisionedBytes := float64(0)
        for host, memTrackersFuture := range tserverMemTrackersFutures {
            memTrackersResult := <-memTrackersFuture
            if memTrackersResult.Error != nil {
                c.logger.Warnf("memTrackersResult from tserver at %s returned error: %s",
                    host, memTrackersResult.Error.Error())
            } else {
                ramProvisionedBytes += float64(memTrackersResult.Limit)
            }
        }
        for host, memTrackersFuture := range masterMemTrackersFutures {
            memTrackersResult := <-memTrackersFuture
            if memTrackersResult.Error != nil {
                c.logger.Warnf("memTrackersResult from master at %s returned error: %s",
                    host, memTrackersResult.Error.Error())
            } else {
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
