package handlers

import (
    "apiserver/cmd/server/helpers"
    "apiserver/cmd/server/models"
    "context"
    "encoding/json"
    "errors"
    "fmt"
    "math"
    "net"
    "net/http"
    "runtime"
    "sort"
    "strconv"
    "strings"
    "time"

    "github.com/jackc/pgx/v4/pgxpool"
    "github.com/labstack/echo/v4"
    "github.com/yugabyte/gocql"
)

const SLOW_QUERY_STATS_SQL string = "SELECT a.rolname, t.datname, t.queryid, " +
    "t.query, t.calls, t.total_time, t.rows, t.min_time, t.max_time, t.mean_time, " +
    "t.stddev_time, t.local_blks_hit, t.local_blks_written FROM " +
    "pg_authid a JOIN (SELECT * FROM " +
    "pg_stat_statements s JOIN pg_database d ON s.dbid = d.oid) t ON a.oid = t.userid"

var EXCLUDED_QUERY_STATEMENTS = map[string]bool{
    "SET extra_float_digits = 3": true,
    SLOW_QUERY_STATS_SQL:         true,
}

// query over one node
const QUERY_FORMAT_NODE string = "select ts, value, details from " +
    "%s where metric = '%s' and node = '%s' and ts >= %d and ts < %d"

// query over over all nodes
const QUERY_FORMAT string = "select ts, value, details from " +
    "%s where metric = '%s' and ts >= %d and ts < %d"

// the count metrics count the total number of accumulated ops, and the sum metric
// counts the total amount of time spent on ops.
const READ_COUNT_METRIC = "handler_latency_yb_tserver_TabletServerService_Read_count"
const WRITE_COUNT_METRIC = "handler_latency_yb_tserver_TabletServerService_Write_count"
const READ_SUM_METRIC = "handler_latency_yb_tserver_TabletServerService_Read_sum"
const WRITE_SUM_METRIC = "handler_latency_yb_tserver_TabletServerService_Write_sum"

const GRANULARITY_NUM_INTERVALS = 120

var OS_NAME = runtime.GOOS

var MAX_PROC = map[string]int{
    "Linux" : 12000,
    "Darwin" : 2500,
}
var WARNING_MSGS = map[string]string{
    "open_files" :"open files ulimits value set low. Please set soft and hard limits to 1048576.",
    "max_user_processes" :fmt.Sprintf("max user processes ulimits value set low." +
        " Please set soft and hard limits to %d", MAX_PROC[OS_NAME]),
    "transparent_hugepages" :"Transparent hugepages disabled. Please enable transparent_hugepages.",
    "ntp/chrony" :"ntp/chrony package is missing for clock synchronization. For centos 7, " +
        "we recommend installing either ntp or chrony package and for centos 8, " +
        "we recommend installing chrony package.",
    "insecure" :"Cluster started in an insecure mode without " +
        "authentication and encryption enabled. For non-production use only, " +
        "not to be used without firewalls blocking the internet traffic.",
}

type SlowQueriesFuture struct {
    Items []*models.SlowQueryResponseYsqlQueryItem
    Error error
}

type DetailObj struct {
    Value float64 `json:"value"`
}

// return hostname of each node
func (c *Container) getNodes(clusterType ...string) ([]string, error) {
    hostNames := []string{}
    tabletServersFuture := make(chan helpers.TabletServersFuture)
    go c.helper.GetTabletServersFuture(helpers.HOST, tabletServersFuture)
    tabletServersResponse := <-tabletServersFuture
    if tabletServersResponse.Error != nil {
        return hostNames, tabletServersResponse.Error
    }

    if len(clusterType) == 0 {
        // to get hostnames, get all second level keys and only keep if
        // net.SpliHostPort succeeds.
        for _, obj := range tabletServersResponse.Tablets {
            for hostport := range obj {
                host, _, err := net.SplitHostPort(hostport)
                if err != nil {
                    c.logger.Warnf("failed to split hostport %s: %s", hostport, err.Error())
                } else {
                    hostNames = append(hostNames, host)
                }
            }
        }
    } else {
        clusterConfigFuture := make(chan helpers.ClusterConfigFuture)
        go c.helper.GetClusterConfigFuture(helpers.HOST, clusterConfigFuture)
        clusterConfigResponse := <-clusterConfigFuture
        if clusterConfigResponse.Error != nil {
            c.logger.Errorf("failed to get cluster config response from %s: %s",
                helpers.HOST, clusterConfigResponse.Error.Error())
            return hostNames, clusterConfigResponse.Error
        }
        replicationInfo := clusterConfigResponse.ClusterConfig.ReplicationInfo
        if clusterType[0] == "READ_REPLICA" {
            readReplicas := replicationInfo.ReadReplicas
            if len(readReplicas) == 0 {
                c.logger.Errorf("no Read Replica nodes present in read replica cluster from %s",
                    helpers.HOST)
                return hostNames, errors.New("no Read Replica nodes present")
            }
            readReplicaUuid := readReplicas[0].PlacementUuid
            for hostport := range tabletServersResponse.Tablets[readReplicaUuid] {
                host, _, err := net.SplitHostPort(hostport)
                if err != nil {
                    c.logger.Warnf("failed to split hostport %s: %s", hostport, err.Error())
                } else {
                    hostNames = append(hostNames, host)
                }
            }
        } else if clusterType[0] == "PRIMARY" {
            primaryUuid := replicationInfo.LiveReplicas.PlacementUuid
            for hostport := range tabletServersResponse.Tablets[primaryUuid] {
                host, _, err := net.SplitHostPort(hostport)
                if err != nil {
                    c.logger.Warnf("failed to split hostport %s: %s", hostport, err.Error())
                } else {
                    hostNames = append(hostNames, host)
                }
            }
        }
    }
    return hostNames, nil
}

func getPreferenceOrder(cloud, region, zone string,
    zonePreferences []helpers.MultiAffinitizedLeader) int32 {
    for i, leader := range zonePreferences {
        for _, cloudInfo := range leader.Zones {
            if cloud == cloudInfo.PlacementCloud &&
               region == cloudInfo.PlacementRegion &&
               zone == cloudInfo.PlacementZone {
                return int32(i+1)
            }
        }
    }
    return -1
}

func getSlowQueriesFuture(nodeHost string, conn *pgxpool.Pool, future chan SlowQueriesFuture) {
    slowQueries := SlowQueriesFuture{
        Items: []*models.SlowQueryResponseYsqlQueryItem{},
        Error: nil,
    }

    rows, err := conn.Query(context.Background(), SLOW_QUERY_STATS_SQL)
    if err != nil {
        slowQueries.Error = err
        future <- slowQueries
        return
    }
    defer rows.Close()

    for rows.Next() {
        rowStruct := models.SlowQueryResponseYsqlQueryItem{}
        err := rows.Scan(&rowStruct.Rolname, &rowStruct.Datname, &rowStruct.Queryid,
            &rowStruct.Query, &rowStruct.Calls, &rowStruct.TotalTime, &rowStruct.Rows,
            &rowStruct.MinTime, &rowStruct.MaxTime, &rowStruct.MeanTime,
            &rowStruct.StddevTime, &rowStruct.LocalBlksHit, &rowStruct.LocalBlksWritten)
        if err != nil {
            slowQueries.Error = err
            future <- slowQueries
            return
        }
        if _, excluded := EXCLUDED_QUERY_STATEMENTS[rowStruct.Query]; !excluded {
            slowQueries.Items = append(slowQueries.Items, &rowStruct)
        }
    }
    err = rows.Err()
    if err != nil {
        slowQueries.Error = err
        future <- slowQueries
        return
    }
    future <- slowQueries
}

// Divides each entry of nodeValuesNumerator by nodeValuesDenominator.
// Assumes that they are the same size, each node is listed in the same order,
// and their timestamps match up.
func divideMetricForAllNodes(
    nodeValuesNumerator [][][]float64,
    nodeValuesDenominator [][][]float64,
) [][][]float64 {
    // we will take minimum lengths just in case the lengths do not match up
    numNodes := len(nodeValuesNumerator)
    if len(nodeValuesDenominator) < numNodes {
        numNodes = len(nodeValuesDenominator)
    }
    resultMetric := make([][][]float64, numNodes)
    for i := 0; i < numNodes; i++ {
        numIntervals := len(nodeValuesNumerator[i])
        if len(nodeValuesDenominator[i]) < numIntervals {
            numIntervals = len(nodeValuesDenominator[i])
        }
        resultMetric[i] = make([][]float64, numIntervals)
        for j := 0; j < numIntervals; j++ {
            if len(nodeValuesNumerator[i][j]) < 2 ||
                len(nodeValuesDenominator[i][j]) < 2 {
                // Handle case where data at window is empty
                resultMetric[i][j] = []float64{nodeValuesNumerator[i][j][0]}
            } else if nodeValuesDenominator[i][j][1] != 0 {
                // Handle divide by 0 case
                // Note: we are comparing a float to 0 to avoid dividing by 0.
                // This will only catch the cases where the float value is exactly 0
                resultMetric[i][j] = []float64{
                    nodeValuesNumerator[i][j][0],
                    nodeValuesNumerator[i][j][1] /
                        nodeValuesDenominator[i][j][1]}
            } else {
                resultMetric[i][j] = []float64{nodeValuesNumerator[i][j][0], 0}
            }
        }
    }
    return resultMetric
}

// Gets the average or sum of a metric over multiple nodes. Assumes that:
// - each [][]float64 in nodeValues has the same intervals,
//   i.e. they are the output of reduceGranularity with the same
//   start/end times and same number of intervals.
// - If isAverage is true, gets the average, otherwise gets the sum
func calculateCombinedMetric(nodeValues [][][]float64, isAverage bool) [][]float64 {
    numNodes := len(nodeValues)
    if numNodes == 0 {
        return [][]float64{}
    }
    if numNodes == 1 {
        return nodeValues[0]
    }
    // we assume all nodes have value array of same length
    numIntervals := len(nodeValues[0])
    newValues := make([][]float64, numIntervals)
    for i := 0; i < numIntervals; i++ {
        newValues[i] = []float64{nodeValues[0][i][0]}
        for j := 0; j < numNodes; j++ {
            value := nodeValues[j][i]
            if len(value) >= 2 {
                if len(newValues[i]) >= 2 {
                    newValues[i][1] += value[1]
                } else {
                    newValues[i] = append(newValues[i], value[1])
                }
            }
        }
    }
    if isAverage {
        for i := 0; i < numIntervals; i++ {
            if len(newValues[i]) >= 2 {
                newValues[i][1] = newValues[i][1] / float64(numNodes)
            }
        }
    }
    return newValues
}

// Get metrics that are meant to be averaged over all nodes. detailsValue is true if the value of
// the metric is in the details column instead of the value column in the system.metrics table.
// Note: assumes values are percentages, and so all values are multiplied by 100
func (c *Container) getAveragePercentageMetricData(
    metricColumnValue string,
    nodeList []string,
    hostToUuid map[string]string,
    startTime int64,
    endTime int64,
    session *gocql.Session,
    detailsValue bool,
) ([][]float64, error) {
    metricValues := [][]float64{}
    rawMetricValues, err := c.getRawMetricsForAllNodes(metricColumnValue, nodeList, hostToUuid,
        startTime, endTime, session, detailsValue)
    if err != nil {
        return metricValues, err
    }
    nodeValues := reduceGranularityForAllNodes(startTime, endTime, rawMetricValues,
        GRANULARITY_NUM_INTERVALS, true)
    metricValues = calculateCombinedMetric(nodeValues, true)
    for i := 0; i < len(metricValues); i++ {
        if len(metricValues[i]) >= 2 {
            metricValues[i][1] *= 100 // multiply by 100 because it is a percentage
        }
    }
    return metricValues, nil
}

// Use this function right before returning GetClusterMetric to specify the number of points
// to display on the graph in the UI.
func reduceGranularity(startTime int64,
    endTime int64, values [][]float64,
    numIntervals int,
    isAverage bool,
) [][]float64 {
    start := float64(startTime)
    end := float64(endTime)
    intervalLength := (end - start) / float64(numIntervals)
    currentTime := start
    newValuesIndex := 0
    counter := 0
    newValues := [][]float64{{start, 0}}
    for i := 0; i < len(values); i++ {
        // keep incrementing window until timestamp fits in interval
        for values[i][0] >= currentTime+intervalLength && newValuesIndex < numIntervals {
            if counter > 1 && isAverage {
                // average out values for this interval
                newValues[newValuesIndex][1] =
                    newValues[newValuesIndex][1] / float64(counter)
            } else if counter == 0 {
                // if no data for this interval, set this timestamp to have no value
                newValues[newValuesIndex] = []float64{newValues[newValuesIndex][0]}
            }
            // increment values for next interval
            currentTime += intervalLength
            newValuesIndex++
            // set count and add to next interval
            counter = 0
            newValues = append(newValues, []float64{currentTime, 0})
        }
        newValues[newValuesIndex][1] += values[i][1]
        counter++
    }
    // ensure the last interval is averaged or removed
    if counter > 1 && isAverage {
        newValues[newValuesIndex][1] = newValues[newValuesIndex][1] / float64(counter)
    } else if counter == 0 {
        newValues[newValuesIndex] = []float64{newValues[newValuesIndex][0]}
    }
    // Finally, add intervals with empty values until we reach endTime
    for end > currentTime+intervalLength && len(newValues) < numIntervals {
        currentTime += intervalLength
        newValues = append(newValues, []float64{currentTime})
    }
    return newValues
}

func reduceGranularityForAllNodes(
    startTime int64,
    endTime int64,
    nodeValues [][][]float64,
    numIntervals int,
    isAverage bool,
) [][][]float64 {
    newNodeValues := make([][][]float64, len(nodeValues))
    for i := 0; i < len(nodeValues); i++ {
        newNodeValues[i] = reduceGranularity(
            startTime,
            endTime,
            nodeValues[i],
            numIntervals,
            isAverage)
    }
    return newNodeValues
}

// Gets raw metrics for all provided nodes. Timestamps are returned in seconds.
// If a node is down, will ignore that node. Will only return an error if all nodes are down.
func (c *Container) getRawMetricsForAllNodes(
    metricColumnValue string,
    nodeList []string,
    hostToUuid map[string]string,
    startTime int64,
    endTime int64,
    session *gocql.Session,
    detailsValue bool,
) ([][][]float64, error) {
    nodeValues := [][][]float64{}
    var ts int64
    var value int
    var details string
    for _, hostName := range nodeList {
        query := fmt.Sprintf(QUERY_FORMAT_NODE, "system.metrics", metricColumnValue,
            hostToUuid[hostName], startTime*1000, endTime*1000)
        iter := session.Query(query).Iter()
        values := [][]float64{}
        for iter.Scan(&ts, &value, &details) {
            if detailsValue {
                detailObj := DetailObj{}
                json.Unmarshal([]byte(details), &detailObj)
                values = append(
                    values,
                    []float64{float64(ts) / 1000, detailObj.Value})
            } else {
                values = append(
                    values,
                    []float64{float64(ts) / 1000, float64(value)})
            }
        }
        if err := iter.Close(); err != nil {
            c.logger.Errorf("Error fetching metrics from %s: %s", hostName, err.Error())
            continue
        }
        sort.Slice(values, func(i, j int) bool {
            return values[i][0] < values[j][0]
        })
        nodeValues = append(nodeValues, values)
    }
    if len(nodeValues) == 0 {
        return nodeValues, errors.New("all nodes failed to return metrics")
    }
    return nodeValues, nil
}

// Converts metrics to rate by dividing difference between consecutive values by difference in time
// Assumes no two consecutive timestamps are equal
func convertRawMetricsToRates(nodeValues [][][]float64) [][][]float64 {
    rateMetrics := [][][]float64{}
    for i := 0; i < len(nodeValues); i++ {
        currentNodeValue := [][]float64{}
        for j := 0; j < len(nodeValues[i])-1; j++ {
            currentNodeValue = append(currentNodeValue,
                []float64{nodeValues[i][j][0],
                    (nodeValues[i][j+1][1] - nodeValues[i][j][1]) /
                        (nodeValues[i][j+1][0] - nodeValues[i][j][0])})
        }
        rateMetrics = append(rateMetrics, currentNodeValue)
    }
    return rateMetrics
}

// Divides every metric value by the provided constant. Modifies metricValues directly.
func divideMetricByConstant(metricValues [][]float64, constant float64) {
    for _, metric := range metricValues {
        if len(metric) >= 2 {
            metric[1] = metric[1] / constant
        }
    }
}

// GetClusterMetric - Get a metric for a cluster
func (c *Container) GetClusterMetric(ctx echo.Context) error {
    metricsParam := strings.Split(ctx.QueryParam("metrics"), ",")
    clusterType := ctx.QueryParam("cluster_type")
    nodeParam := ctx.QueryParam("node_name")
    nodeList := []string{nodeParam}
    var err error = nil
    if nodeParam == "" {
        if clusterType == "" {
            nodeList, err = c.getNodes()
        } else if clusterType == "PRIMARY" {
            nodeList, err = c.getNodes("PRIMARY")
        } else if clusterType == "READ_REPLICA" {
            nodeList, err = c.getNodes("READ_REPLICA")
        }
        if err != nil {
            c.logger.Errorf("[getNodes]: %s", err.Error())
            return ctx.String(http.StatusInternalServerError, err.Error())
        }
    }
    hostToUuid, err := c.helper.GetHostToUuidMap(helpers.HOST)
    if err != nil {
        c.logger.Errorf("[GetHostToUuidMap]: %s", err.Error())
        return ctx.String(http.StatusInternalServerError, err.Error())
    }
    // in case of errors parsing start/end time, set default start = 1 hour ago, end = now
    startTime, err := strconv.ParseInt(ctx.QueryParam("start_time"), 10, 64)
    if err != nil {
        c.logger.Warnf("error parsing start time for metric, defaulting to 1 hour ago")
        now := time.Now()
        startTime = now.Unix() - 60*60
    }
    endTime, err := strconv.ParseInt(ctx.QueryParam("end_time"), 10, 64)
    if err != nil {
        c.logger.Warnf("error parsing end time for metric, defaulting to now")
        now := time.Now()
        endTime = now.Unix()
    }

    metricResponse := models.MetricResponse{
        Data:           []models.MetricData{},
        StartTimestamp: startTime,
        EndTimestamp:   endTime,
    }

    session, err := c.GetSession()
    if err != nil {
        c.logger.Errorf("[GetSession]: %s", err.Error())
        return ctx.String(http.StatusInternalServerError, err.Error())
    }

    for _, metric := range metricsParam {
        switch metric {
        case "READ_OPS_PER_SEC":
            rawMetricValues, err := c.getRawMetricsForAllNodes(READ_COUNT_METRIC,
                nodeList, hostToUuid, startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }
            rateMetrics := convertRawMetricsToRates(rawMetricValues)
            nodeMetricValues := reduceGranularityForAllNodes(startTime, endTime,
                rateMetrics, GRANULARITY_NUM_INTERVALS, true)
            metricValues := calculateCombinedMetric(nodeMetricValues, false)
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name:   metric,
                Values: metricValues,
            })
        case "WRITE_OPS_PER_SEC":
            rawMetricValues, err := c.getRawMetricsForAllNodes(WRITE_COUNT_METRIC,
                nodeList, hostToUuid, startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }
            rateMetrics := convertRawMetricsToRates(rawMetricValues)
            nodeMetricValues := reduceGranularityForAllNodes(startTime, endTime,
                rateMetrics, GRANULARITY_NUM_INTERVALS, true)
            metricValues := calculateCombinedMetric(nodeMetricValues, false)
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name:   metric,
                Values: metricValues,
            })
        case "CPU_USAGE_USER":
            metricValues, err := c.getAveragePercentageMetricData("cpu_usage_user",
                nodeList, hostToUuid, startTime, endTime, session, true)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name:   metric,
                Values: metricValues,
            })
        case "CPU_USAGE_SYSTEM":
            metricValues, err := c.getAveragePercentageMetricData("cpu_usage_system",
                nodeList, hostToUuid, startTime, endTime, session, true)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name:   metric,
                Values: metricValues,
            })
        case "DISK_USAGE_GB":
            reducedNodeList := c.helper.RemoveLocalAddresses(nodeList)
            rawTotalDiskValues, err := c.getRawMetricsForAllNodes("total_disk",
                reducedNodeList, hostToUuid, startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }
            nodeTotalDiskValues := reduceGranularityForAllNodes(startTime, endTime,
                rawTotalDiskValues, GRANULARITY_NUM_INTERVALS, true)
            combinedTotalDiskValues := calculateCombinedMetric(nodeTotalDiskValues, false)
            divideMetricByConstant(combinedTotalDiskValues, helpers.BYTES_IN_GB)
            rawFreeDiskValues, err := c.getRawMetricsForAllNodes("free_disk",
                reducedNodeList, hostToUuid, startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }
            nodeFreeDiskValues := reduceGranularityForAllNodes(startTime, endTime,
                rawFreeDiskValues, GRANULARITY_NUM_INTERVALS, true)
            combinedFreeDiskValues := calculateCombinedMetric(nodeFreeDiskValues, false)
            // We divide by negative value so we can sum with total disk metric
            divideMetricByConstant(combinedFreeDiskValues, -helpers.BYTES_IN_GB)
            combinedDiskUsageValues := calculateCombinedMetric(
                [][][]float64{combinedTotalDiskValues, combinedFreeDiskValues}, false)
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name: metric,
                Values: combinedDiskUsageValues,
            })
        case "PROVISIONED_DISK_SPACE_GB":
            reducedNodeList := c.helper.RemoveLocalAddresses(nodeList)
            rawMetricValues, err := c.getRawMetricsForAllNodes("total_disk",
                reducedNodeList, hostToUuid, startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }
            nodeValues := reduceGranularityForAllNodes(startTime, endTime, rawMetricValues,
                GRANULARITY_NUM_INTERVALS, true)
            combinedValues := calculateCombinedMetric(nodeValues, false)
            divideMetricByConstant(combinedValues, helpers.BYTES_IN_GB)
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name: metric,
                Values: combinedValues,
            })
        case "AVERAGE_READ_LATENCY_MS":
            rawMetricValuesCount, err := c.getRawMetricsForAllNodes(READ_COUNT_METRIC,
                nodeList, hostToUuid, startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }

            rawMetricValuesSum, err := c.getRawMetricsForAllNodes(READ_SUM_METRIC,
                nodeList, hostToUuid, startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }

            rateMetricsCount := convertRawMetricsToRates(rawMetricValuesCount)
            rateMetricsSum := convertRawMetricsToRates(rawMetricValuesSum)

            rateMetricsCountReduced := reduceGranularityForAllNodes(startTime, endTime,
                rateMetricsCount, GRANULARITY_NUM_INTERVALS, false)

            rateMetricsSumReduced := reduceGranularityForAllNodes(startTime, endTime,
                rateMetricsSum, GRANULARITY_NUM_INTERVALS, false)

            rateMetricsCountCombined :=
                calculateCombinedMetric(rateMetricsCountReduced, false)
            rateMetricsSumCombined :=
                calculateCombinedMetric(rateMetricsSumReduced, false)

            latencyMetric :=
                divideMetricForAllNodes([][][]float64{rateMetricsSumCombined},
                    [][][]float64{rateMetricsCountCombined})

            metricValues := latencyMetric[0]
            // Divide everything by 1000 to convert from microseconds to milliseconds
            divideMetricByConstant(metricValues, 1000)
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name:   metric,
                Values: metricValues,
            })
        case "AVERAGE_WRITE_LATENCY_MS":
            rawMetricValuesCount, err := c.getRawMetricsForAllNodes(WRITE_COUNT_METRIC,
                nodeList, hostToUuid, startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }

            rawMetricValuesSum, err := c.getRawMetricsForAllNodes(WRITE_SUM_METRIC,
                nodeList, hostToUuid, startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }

            rateMetricsCount := convertRawMetricsToRates(rawMetricValuesCount)
            rateMetricsSum := convertRawMetricsToRates(rawMetricValuesSum)

            rateMetricsCountReduced := reduceGranularityForAllNodes(startTime, endTime,
                rateMetricsCount, GRANULARITY_NUM_INTERVALS, false)

            rateMetricsSumReduced := reduceGranularityForAllNodes(startTime, endTime,
                rateMetricsSum, GRANULARITY_NUM_INTERVALS, false)

            rateMetricsCountCombined :=
                calculateCombinedMetric(rateMetricsCountReduced, false)
            rateMetricsSumCombined :=
                calculateCombinedMetric(rateMetricsSumReduced, false)

            latencyMetric :=
                divideMetricForAllNodes([][][]float64{rateMetricsSumCombined},
                    [][][]float64{rateMetricsCountCombined})

            metricValues := latencyMetric[0]
            // Divide everything by 1000 to convert from microseconds to milliseconds
            divideMetricByConstant(metricValues, 1000)
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name:   metric,
                Values: metricValues,
            })
        case "TOTAL_LIVE_NODES":
            rawMetricValues, err := c.getRawMetricsForAllNodes("node_up", nodeList,
                hostToUuid, startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }
            reducedMetric := reduceGranularityForAllNodes(startTime, endTime,
                rawMetricValues, GRANULARITY_NUM_INTERVALS, true)
            metricValues := calculateCombinedMetric(reducedMetric, false)
            // In cases where there is no data, set to 0
            for i, metric := range metricValues {
                if len(metric) < 2 {
                    metricValues[i] = append(metricValues[i], 0)
                }
            }
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name:   metric,
                Values: metricValues,
            })
        case "TOTAL_LOGICAL_CONNECTIONS":
            rawMetricValues, err := c.getRawMetricsForAllNodes("total_logical_connections",
                nodeList, hostToUuid, startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }
            // If the time window is less than 1 hour, don't use as many intervals
            numIntervals := GRANULARITY_NUM_INTERVALS
            if endTime - startTime < 60*60 {
                numIntervals = 25
            }
            nodeMetricValues := reduceGranularityForAllNodes(startTime, endTime,
                rawMetricValues, numIntervals, true)
            metricValues := calculateCombinedMetric(nodeMetricValues, false)
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name:   metric,
                Values: metricValues,
            })
        case "TOTAL_PHYSICAL_CONNECTIONS":
            rawMetricValues, err := c.getRawMetricsForAllNodes("total_physical_connections",
            nodeList, hostToUuid, startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }
            // If the time window is less than 1 hour, don't use as many intervals
            numIntervals := GRANULARITY_NUM_INTERVALS
            if endTime - startTime < 60*60 {
                numIntervals = 25
            }
            nodeMetricValues := reduceGranularityForAllNodes(startTime, endTime,
                rawMetricValues, numIntervals, true)
            metricValues := calculateCombinedMetric(nodeMetricValues, false)
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name:   metric,
                Values: metricValues,
            })
        }
    }
    return ctx.JSON(http.StatusOK, metricResponse)
}

// GetClusterActivities - Get the cluster activities details
func (c *Container) GetClusterActivities(ctx echo.Context) error {
    response := models.ActivitiesResponse{
            Data: []models.ActivityData{},
    }

    statusParam := ctx.QueryParam("status")
    activityParam := strings.Split(ctx.QueryParam("activities"), ",")

    for _, activity := range activityParam {
        switch activity {
        case "INDEX_BACKFILL":
            switch statusParam {
            case "COMPLETED":
                completedIndexBackFills := c.helper.GetCompletedIndexBackFillInfo()
                if completedIndexBackFills.Error != nil {
                    return ctx.String(http.StatusInternalServerError,
                        completedIndexBackFills.Error.Error())
                }
                for _, completedActivityInfo := range completedIndexBackFills.IndexBackFillInfo {
                    response.Data = append(response.Data, models.ActivityData{
                        Name: activity,
                        Data: completedActivityInfo,
                    })
                }
            case "IN_PROGRESS":
                databaseParam := ctx.QueryParam("database")
                if databaseParam == "" {
                    databaseParam = "yugabyte"
                }
                nodes, err := c.getNodes()
                if err != nil {
                    c.logger.Errorf("[getNodes]: %s", err.Error())
                    return ctx.String(http.StatusInternalServerError, err.Error())
                }

                futures := []chan helpers.IndexBackFillInfoFuture{}
                for _, nodeHost := range nodes {
                    conn, err := c.GetConnectionFromMap(nodeHost, databaseParam)
                    if err == nil {
                        future := make(chan helpers.IndexBackFillInfoFuture)
                        futures = append(futures, future)
                        go c.helper.GetIndexBackFillInfo(conn, future)
                    }
                }
                for _, future := range futures {
                    indexBackFillInfoResponse := <-future
                    if indexBackFillInfoResponse.Error != nil {
                        return ctx.String(http.StatusInternalServerError,
                            indexBackFillInfoResponse.Error.Error())
                    }
                    for _, indexBackFillInfo := range indexBackFillInfoResponse.IndexBackFillInfo {
                        response.Data = append(response.Data, models.ActivityData{
                            Name: activity,
                            Data: indexBackFillInfo,
                        })
                    }
                }
            }
        }
    }
    return ctx.JSON(http.StatusOK, response)
}

// GetClusterNodes - Get the nodes for a cluster
func (c *Container) GetClusterNodes(ctx echo.Context) error {
    response := models.ClusterNodesResponse{
        Data: []models.NodeData{},
    }
    tabletServersFuture := make(chan helpers.TabletServersFuture)
    clusterConfigFuture := make(chan helpers.ClusterConfigFuture)
    go c.helper.GetTabletServersFuture(helpers.HOST, tabletServersFuture)
    go c.helper.GetClusterConfigFuture(helpers.HOST, clusterConfigFuture)
    tabletServersResponse := <-tabletServersFuture
    if tabletServersResponse.Error != nil {
        c.logger.Errorf("[tabletServersResponse]: %s", tabletServersResponse.Error.Error())
        return ctx.String(http.StatusInternalServerError,
            tabletServersResponse.Error.Error())
    }
    // Use the cluster config API to get the read-replica (If any) placement UUID
    clusterConfigResponse := <-clusterConfigFuture
    readReplicaUuid := ""
    if clusterConfigResponse.Error == nil {
        for _, replica := range clusterConfigResponse.
            ClusterConfig.ReplicationInfo.ReadReplicas {
            readReplicaUuid = replica.PlacementUuid
        }
    }
    mastersFuture := make(chan helpers.MastersFuture)
    go c.helper.GetMastersFuture(mastersFuture)

    nodeList := c.helper.GetNodesList(tabletServersResponse)
    versionInfoFutures := map[string]chan helpers.VersionInfoFuture{}
    for _, nodeHost := range nodeList {
        versionInfoFuture := make(chan helpers.VersionInfoFuture)
        versionInfoFutures[nodeHost] = versionInfoFuture
        go c.helper.GetVersionFuture(nodeHost, false, versionInfoFuture)
    }
    activeYsqlConnectionsFutures := map[string]chan helpers.ActiveYsqlConnectionsFuture{}
    activeYcqlConnectionsFutures := map[string]chan helpers.ActiveYcqlConnectionsFuture{}
    tserverMemTrackersFutures := map[string]chan helpers.MemTrackersFuture{}
    for _, nodeHost := range nodeList {
        activeYsqlConnectionsFuture := make(chan helpers.ActiveYsqlConnectionsFuture)
        activeYsqlConnectionsFutures[nodeHost] = activeYsqlConnectionsFuture
        go c.helper.GetActiveYsqlConnectionsFuture(nodeHost, activeYsqlConnectionsFuture)
        activeYcqlConnectionsFuture := make(chan helpers.ActiveYcqlConnectionsFuture)
        activeYcqlConnectionsFutures[nodeHost] = activeYcqlConnectionsFuture
        go c.helper.GetActiveYcqlConnectionsFuture(nodeHost, activeYcqlConnectionsFuture)
        tserverMemTrackerFuture := make(chan helpers.MemTrackersFuture)
        tserverMemTrackersFutures[nodeHost] = tserverMemTrackerFuture
        go c.helper.GetMemTrackersFuture(nodeHost, false, tserverMemTrackerFuture)
    }
    masters := map[string]helpers.Master{}
    mastersResponse := <-mastersFuture
    if mastersResponse.Error == nil {
        for _, master := range mastersResponse.Masters {
            if len(master.Registration.PrivateRpcAddresses) > 0 {
                masters[master.Registration.PrivateRpcAddresses[0].Host] = master
            }
        }
    }
    // Need to get versions of masters as well
    versionInfoMasterFutures := map[string]chan helpers.VersionInfoFuture{}
    masterMemTrackersFutures := map[string]chan helpers.MemTrackersFuture{}
    for masterHost := range masters {
        versionInfoFuture := make(chan helpers.VersionInfoFuture)
        versionInfoMasterFutures[masterHost] = versionInfoFuture
        go c.helper.GetVersionFuture(masterHost, true, versionInfoFuture)
        masterMemTrackerFuture := make(chan helpers.MemTrackersFuture)
        masterMemTrackersFutures[masterHost] = masterMemTrackerFuture
        go c.helper.GetMemTrackersFuture(masterHost, true, masterMemTrackerFuture)
    }

    currentTime := time.Now().UnixMicro()
    hostToUuid, errHostToUuidMap := c.helper.GetHostToUuidMap(helpers.HOST)
    tserverAddresses := map[string]bool{}
    for placementUuid, obj := range tabletServersResponse.Tablets {
        // Cross check the placement UUID of the node with that of read-replica cluster
        isReadReplica := false
        if readReplicaUuid != "" && readReplicaUuid == placementUuid {
            isReadReplica = true
        }
        // Keep track of tserver addresses
        for hostport, nodeData := range obj {
            host, _, err := net.SplitHostPort(hostport)
            // If we can split hostport, just use host as name.
            // Otherwise, use hostport as name.
            // However, we can only get version information if we can get the host
            hostName := hostport
            versionNumber := ""
            activeYsqlConnections := int64(0)
            activeYcqlConnections := int64(0)
            isMasterUp := false
            ramUsedTserver := int64(0)
            ramUsedMaster := int64(0)
            ramLimitTserver := int64(0)
            ramLimitMaster := int64(0)
            masterUptimeUs := int64(0)
            totalDiskBytes := int64(0)
            if err != nil {
                c.logger.Warnf("failed to split host/port: %s: %s", hostport, err.Error())
            } else {
                hostName = host
                tserverAddresses[hostName] = true
                versionInfo := <-versionInfoFutures[hostName]
                versionInfoMaster := helpers.VersionInfoFuture{
                    Error: errors.New("tserver has no master, don't get master version info"),
                }
                if _, ok := versionInfoMasterFutures[hostName]; ok {
                    versionInfoMaster = <-versionInfoMasterFutures[hostName]
                }
                if versionInfo.Error == nil {
                    if versionInfoMaster.Error == nil &&
                        c.helper.CompareVersions(versionInfo.VersionInfo.VersionNumber,
                            versionInfoMaster.VersionInfo.VersionNumber) > 0 {
                        versionNumber = versionInfoMaster.VersionInfo.VersionNumber
                    } else {
                        versionNumber = versionInfo.VersionInfo.VersionNumber
                    }
                } else if versionInfoMaster.Error == nil {
                    versionNumber = versionInfoMaster.VersionInfo.VersionNumber
                }
                ysqlConnections := <-activeYsqlConnectionsFutures[hostName]
                if ysqlConnections.Error == nil {
                    activeYsqlConnections += ysqlConnections.YsqlConnections
                }
                ycqlConnections := <-activeYcqlConnectionsFutures[hostName]
                if ycqlConnections.Error == nil {
                    activeYcqlConnections += ycqlConnections.YcqlConnections
                }
                masterMemTracker := helpers.MemTrackersFuture{
                    Error: errors.New("tserver has no master, don't get master mem tracker"),
                }
                if _, ok := masterMemTrackersFutures[hostName]; ok {
                    masterMemTracker = <-masterMemTrackersFutures[hostName]
                }
                if masterMemTracker.Error == nil {
                    ramUsedMaster = masterMemTracker.Consumption
                    ramLimitMaster = masterMemTracker.Limit
                }
                tserverMemTracker := <-tserverMemTrackersFutures[hostName]
                if tserverMemTracker.Error == nil {
                    ramUsedTserver = tserverMemTracker.Consumption
                    ramLimitTserver = tserverMemTracker.Limit
                }
                if master, ok := masters[hostName]; ok {
                    isMasterUp = master.Error == nil
                    if isMasterUp {
                        masterUptimeUs = currentTime - master.InstanceId.StartTimeUs
                    }
                }
                if errHostToUuidMap == nil {
                    query :=
                        fmt.Sprintf(QUERY_LIMIT_ONE, "system.metrics", "total_disk",
                            hostToUuid[hostName])
                    session, err := c.GetSession()
                    if err == nil {
                        iter := session.Query(query).Iter()
                        var ts int64
                        var value int64
                        var details string
                        iter.Scan(&ts, &value, &details)
                        totalDiskBytes = value
                        if err := iter.Close(); err != nil {
                            c.logger.Errorf("Error fetching total_disk from %s: %s",
                                hostName, err.Error())
                        }
                    }
                }
            }
            totalSstFileSizeBytes := int64(nodeData.TotalSstFileSizeBytes)
            uncompressedSstFileSizeBytes :=
                int64(nodeData.UncompressedSstFileSizeBytes)
            userTabletsTotal := int64(nodeData.UserTabletsTotal)
            userTabletsLeaders := int64(nodeData.UserTabletsLeaders)
            systemTabletsTotal := int64(nodeData.SystemTabletsTotal)
            systemTabletsLeaders := int64(nodeData.SystemTabletsLeaders)
            activeConnections := models.NodeDataMetricsActiveConnections{
                Ysql: activeYsqlConnections,
                Ycql: activeYcqlConnections,
            }
            ramUsedBytes := ramUsedMaster + ramUsedTserver
            ramProvisionedBytes := ramLimitMaster + ramLimitTserver
            isBootstrapping := true
            // For now we hard code isBootstrapping here, and we use the
            // GetIsLoadBalancerIdle endpoint separately to determine if
            // a node is bootstrapping on the frontend, since yb-admin is a
            // bit slow. Once we get a faster way of doing this we can move
            // the implementation here.
            // For now, assuming IsTserver is always true
            _, isMaster := masters[hostName]
            zonePreferences := clusterConfigResponse.ClusterConfig.
            ReplicationInfo.MultiAffinitizedLeaders
            var preferenceOrder int32 = getPreferenceOrder(
                nodeData.Cloud, nodeData.Region, nodeData.Zone,
                zonePreferences)
            response.Data = append(response.Data, models.NodeData{
                Name:            hostName,
                Host:            hostName,
                IsNodeUp:        nodeData.Status == "ALIVE",
                IsMaster:        isMaster,
                IsTserver:       true,
                IsReadReplica:   isReadReplica,
                PreferenceOrder: preferenceOrder,
                IsMasterUp:      isMasterUp,
                IsBootstrapping: isBootstrapping,
                Metrics: models.NodeDataMetrics{
                    // Eventually we want to change models.NodeDataMetrics so that
                    // all the int64 fields are uint64. But currently openapi
                    // generator only generates int64s. Ideally if we set
                    // minimum: 0 in the specs, the generator should use uint64.
                    // We should try to implement this into openapi-generator.
                    MemoryUsedBytes:              int64(nodeData.RamUsedBytes),
                    TotalSstFileSizeBytes:        &totalSstFileSizeBytes,
                    UncompressedSstFileSizeBytes: &uncompressedSstFileSizeBytes,
                    ReadOpsPerSec:                nodeData.ReadOpsPerSec,
                    WriteOpsPerSec:               nodeData.WriteOpsPerSec,
                    TimeSinceHbSec:               nodeData.TimeSinceHbSec,
                    UptimeSeconds:                int64(nodeData.UptimeSeconds),
                    UserTabletsTotal:             userTabletsTotal,
                    UserTabletsLeaders:           userTabletsLeaders,
                    SystemTabletsTotal:           systemTabletsTotal,
                    SystemTabletsLeaders:         systemTabletsLeaders,
                    ActiveConnections:            activeConnections,
                    MasterUptimeUs:               masterUptimeUs,
                    RamUsedBytes:                 ramUsedBytes,
                    RamProvisionedBytes:          ramProvisionedBytes,
                    DiskProvisionedBytes:         totalDiskBytes,
                },
                CloudInfo: models.NodeDataCloudInfo{
                    Cloud:  nodeData.Cloud,
                    Region: nodeData.Region,
                    Zone:   nodeData.Zone,
                },
                SoftwareVersion: versionNumber,
            })
        }
    }
    // Special case for deployments where there are master-only nodes (not tservers)
    getAllMasters := ctx.QueryParam("get_all_masters")
    if getAllMasters != "" {
        // Add data for masters that have no tserver. Will have missing values
        for masterHost, masterData := range masters {
            if _, ok := tserverAddresses[masterHost]; !ok {
                isMasterUp := masterData.Error == nil
                if !isMasterUp {
                    response.Data = append(response.Data, models.NodeData{
                        Name:            masterHost,
                        Host:            masterHost,
                        IsNodeUp:        isMasterUp,
                        IsMaster:        true,
                        IsTserver:       false,
                        IsMasterUp:      isMasterUp,
                    })
                    continue
                }
                isReadReplica := false
                if readReplicaUuid != "" &&
                    readReplicaUuid == masterData.Registration.PlacementUuid {
                    isReadReplica = true
                }
                zonePreferences := clusterConfigResponse.ClusterConfig.
                    ReplicationInfo.MultiAffinitizedLeaders
                var preferenceOrder int32 = getPreferenceOrder(
                    masterData.Registration.CloudInfo.PlacementCloud,
                    masterData.Registration.CloudInfo.PlacementRegion,
                    masterData.Registration.CloudInfo.PlacementZone,
                    zonePreferences)
                masterMemTracker := <-masterMemTrackersFutures[masterHost]
                ramUsedMaster := int64(0)
                ramLimitMaster := int64(0)
                if masterMemTracker.Error == nil {
                    ramUsedMaster = masterMemTracker.Consumption
                    ramLimitMaster = masterMemTracker.Limit
                }
                masterUptimeUs := int64(currentTime - masterData.InstanceId.StartTimeUs)
                versionNumber := ""
                versionInfoMaster := <-versionInfoMasterFutures[masterHost]
                if versionInfoMaster.Error == nil {
                    versionNumber = versionInfoMaster.VersionInfo.VersionNumber
                }
                response.Data = append(response.Data, models.NodeData{
                    Name:            masterHost,
                    Host:            masterHost,
                    IsNodeUp:        isMasterUp,
                    IsMaster:        true,
                    IsTserver:       false,
                    IsReadReplica:   isReadReplica,
                    PreferenceOrder: preferenceOrder,
                    IsMasterUp:      isMasterUp,
                    IsBootstrapping: true,
                    Metrics: models.NodeDataMetrics{
                        // Some fields are redundant, keep them in for safety reasons
                        MemoryUsedBytes:              ramUsedMaster,
                        UptimeSeconds:                masterUptimeUs / 1000000,
                        MasterUptimeUs:               masterUptimeUs,
                        RamUsedBytes:                 ramUsedMaster,
                        RamProvisionedBytes:          ramLimitMaster,
                    },
                    CloudInfo: models.NodeDataCloudInfo{
                        Cloud:  masterData.Registration.CloudInfo.PlacementCloud,
                        Region: masterData.Registration.CloudInfo.PlacementRegion,
                        Zone:   masterData.Registration.CloudInfo.PlacementZone,
                    },
                    SoftwareVersion: versionNumber,
                })
            }
        }
    }
    sort.Slice(response.Data, func(i, j int) bool {
        return response.Data[i].Name < response.Data[j].Name
    })
    return ctx.JSON(http.StatusOK, response)
}

// GetClusterTables - Get list of DB tables per YB API (YCQL/YSQL)
func (c *Container) GetClusterTables(ctx echo.Context) error {
    tableListResponse := models.ClusterTableListResponse{
        Tables: []models.ClusterTable{},
        Indexes: []models.ClusterTable{},
    }
    tablesFuture := make(chan helpers.TablesFuture)
    go c.helper.GetTablesFuture(helpers.HOST, true, tablesFuture)
    tablesListStruct := <-tablesFuture
    if tablesListStruct.Error != nil {
        return ctx.String(http.StatusInternalServerError, tablesListStruct.Error.Error())
    }
    // For now, we only show user and index tables.
    tablesList := tablesListStruct.Tables.User
    indexesList := tablesListStruct.Tables.Index
    api := ctx.QueryParam("api")
    switch api {
    case "YSQL":
        for _, table := range tablesList {
            if table.YsqlOid != "" {
                tableListResponse.Tables = append(tableListResponse.Tables,
                    models.ClusterTable{
                        Name:      table.TableName,
                        Keyspace:  table.Keyspace,
                        Uuid:      table.Uuid,
                        Type:      models.YBAPIENUM_YSQL,
                        SizeBytes: table.OnDiskSize.WalFilesSizeBytes +
                                   table.OnDiskSize.SstFilesSizeBytes,
                    })
            }
        }
        for _, index := range indexesList {
            if index.YsqlOid != "" {
                tableListResponse.Indexes = append(tableListResponse.Indexes,
                    models.ClusterTable{
                        Name:      index.TableName,
                        Keyspace:  index.Keyspace,
                        Uuid:      index.Uuid,
                        Type:      models.YBAPIENUM_YSQL,
                        SizeBytes: index.OnDiskSize.WalFilesSizeBytes +
                                   index.OnDiskSize.SstFilesSizeBytes,
                    })
            }
        }
    case "YCQL":
        for _, table := range tablesList {
            if table.YsqlOid == "" {
                tableListResponse.Tables = append(tableListResponse.Tables,
                    models.ClusterTable{
                        Name:      table.TableName,
                        Keyspace:  table.Keyspace,
                        Uuid:      table.Uuid,
                        Type:      models.YBAPIENUM_YCQL,
                        SizeBytes: table.OnDiskSize.WalFilesSizeBytes +
                                   table.OnDiskSize.SstFilesSizeBytes,
                })
            }
        }
    }
    return ctx.JSON(http.StatusOK, tableListResponse)
}

// GetClusterHealthCheck - Get health information about the cluster
func (c *Container) GetClusterHealthCheck(ctx echo.Context) error {
    future := make(chan helpers.HealthCheckFuture)
    go c.helper.GetHealthCheckFuture(helpers.HOST, future)
    result := <-future
    if result.Error != nil {
        c.logger.Errorf("[GetHealthCheckFuture]: %s", result.Error.Error())
        return ctx.String(http.StatusInternalServerError, result.Error.Error())
    }
    return ctx.JSON(http.StatusOK, models.HealthCheckResponse{
        Data: models.HealthCheckInfo{
            DeadNodes:              result.HealthCheck.DeadNodes,
            MostRecentUptime:       result.HealthCheck.MostRecentUptime,
            UnderReplicatedTablets: result.HealthCheck.UnderReplicatedTablets,
        },
    })
}

// GetLiveQueries - Get the live queries in a cluster
func (c *Container) GetLiveQueries(ctx echo.Context) error {
    api := ctx.QueryParam("api")
    liveQueryResponse := models.LiveQueryResponseSchema{
        Data: models.LiveQueryResponseData{},
    }
    nodes, err := c.getNodes()
    if err != nil {
        c.logger.Errorf("[getNodes]: %s", err.Error())
        return ctx.String(http.StatusInternalServerError, err.Error())
    }
    if api == "YSQL" {
        liveQueryResponse.Data.Ysql = models.LiveQueryResponseYsqlData{
            ErrorCount: 0,
            Queries:    []models.LiveQueryResponseYsqlQueryItem{},
        }
        // Get live queries of all nodes in parallel
        futures := map[string]chan helpers.LiveQueriesYsqlFuture{}
        for _, nodeHost := range nodes {
            future := make(chan helpers.LiveQueriesYsqlFuture)
            futures[nodeHost] = future
            go c.helper.GetLiveQueriesYsqlFuture(nodeHost, future)
        }
        for nodeHost, future := range futures {
            items := <-future
            if items.Error != nil {
                c.logger.Warnf("error getting live ysql queries from %s: %s",
                    nodeHost, items.Error.Error())
                liveQueryResponse.Data.Ysql.ErrorCount++
                continue
            }
            for _, item := range items.Items {
                liveQueryResponse.Data.Ysql.Queries =
                    append(liveQueryResponse.Data.Ysql.Queries, *item)
            }
        }
    }
    if api == "YCQL" {
        liveQueryResponse.Data.Ycql = models.LiveQueryResponseYcqlData{
            ErrorCount: 0,
            Queries:    []models.LiveQueryResponseYcqlQueryItem{},
        }
        // Get live queries of all nodes in parallel
        futures := map[string]chan helpers.LiveQueriesYcqlFuture{}
        for _, nodeHost := range nodes {
            future := make(chan helpers.LiveQueriesYcqlFuture)
            futures[nodeHost] = future
            go c.helper.GetLiveQueriesYcqlFuture(nodeHost, future)
        }
        for nodeHost, future := range futures {
            items := <-future
            if items.Error != nil {
                c.logger.Warnf("error getting live ycql queries from %s: %s",
                    nodeHost, items.Error.Error())
                liveQueryResponse.Data.Ycql.ErrorCount++
                continue
            }
            for _, item := range items.Items {
                liveQueryResponse.Data.Ycql.Queries =
                    append(liveQueryResponse.Data.Ycql.Queries, *item)
            }
        }
    }
    return ctx.JSON(http.StatusOK, liveQueryResponse)
}

// GetSlowQueries - Get the slow queries in a cluster
func (c *Container) GetSlowQueries(ctx echo.Context) error {
    nodes, err := c.getNodes()
    if err != nil {
        c.logger.Errorf("[getNodes]: %s", err.Error())
        return ctx.String(http.StatusInternalServerError, err.Error())
    }
    slowQueryResponse := models.SlowQueryResponseSchema{
        Data: models.SlowQueryResponseData{
            Ysql: models.SlowQueryResponseYsqlData{
                ErrorCount: 0,
                Queries:    []models.SlowQueryResponseYsqlQueryItem{},
            },
        },
    }

    // for each node, get slow queries and aggregate the stats.
    // do each node in parallel
    futures := map[string]chan SlowQueriesFuture{}
    for _, nodeHost := range nodes {
        conn, err := c.GetConnectionFromMap(nodeHost)
        if err != nil {
            c.logger.Errorf("[GetConnectionFromMap]: %s", err.Error())
        } else {
            future := make(chan SlowQueriesFuture)
            futures[nodeHost] = future
            go getSlowQueriesFuture(nodeHost, conn, future)
        }
    }
    // Keep track of stats for each query so we can aggregrate the states over all nodes
    queryMap := map[string]*models.SlowQueryResponseYsqlQueryItem{}
    for nodeHost, future := range futures {
        items := <-future
        if items.Error != nil {
            c.logger.Warnf("error getting slow queries from %s: %s", nodeHost, items.Error.Error())
            slowQueryResponse.Data.Ysql.ErrorCount++
            continue
        }
        for _, item := range items.Items {
            if val, ok := queryMap[item.Query]; ok {
                // If the query is already in the map, we update its stats

                // item is new query, val is previous queries

                // Defining values to reuse.
                X_a := val.MeanTime
                X_b := item.MeanTime
                n_a := float64(val.Calls)
                n_b := float64(item.Calls)
                S_a := val.StddevTime
                S_b := item.StddevTime

                val.TotalTime += item.TotalTime
                val.Calls += item.Calls
                val.Rows += item.Rows
                val.MaxTime = math.Max(float64(val.MaxTime), float64(item.MaxTime))
                val.MinTime = math.Min(float64(val.MinTime), float64(item.MinTime))
                val.LocalBlksWritten += item.LocalBlksWritten
                /*
                 * Formula to calculate std dev of two samples:
                 * Let mean, std dev, and size of
                 * sample A be X_a, S_a, n_a respectively;
                 * and mean, std dev, and size of sample B
                 * be X_b, S_b, n_b respectively.
                 * Then mean of combined sample X is given by
                 *     n_a X_a + n_b X_b
                 * X = -----------------
                 *         n_a + n_b
                 *
                 * The std dev of combined sample S is
                 *           n_a ( S_a^2 + (X_a - X)^2) + n_b(S_b^2 + (X_b - X)^2)
                 * S = sqrt( ----------------------------------------------------- )
                 *                                 n_a + n_b
                 */
                totalCalls := float64(val.Calls)
                averageTime := (n_a*X_a + n_b*X_b) / totalCalls
                stdDevTime := math.Sqrt(
                    (n_a*(math.Pow(S_a, 2)+math.Pow(X_a-averageTime, 2)) +
                        n_b*(math.Pow(S_b, 2)+math.Pow(X_b-averageTime, 2))) /
                        totalCalls)
                val.MeanTime = averageTime
                val.StddevTime = stdDevTime
            } else {
                // If the query is not already in the map, add it to the map.
                queryMap[item.Query] = item
            }
        }
    }
    // put queries into slice and return
    for _, value := range queryMap {
        slowQueryResponse.Data.Ysql.Queries =
            append(slowQueryResponse.Data.Ysql.Queries, *value)
    }
    return ctx.JSON(http.StatusOK, slowQueryResponse)
}

// GetLiveQueries - Get the tablets in a cluster
func (c *Container) GetClusterTablets(ctx echo.Context) error {
    tabletListResponse := models.ClusterTabletListResponse{
        Data: map[string]models.ClusterTablet{},
    }
    // We need to aggregate results from all tservers
    nodes, err := c.getNodes()
    if err != nil {
        c.logger.Errorf("[getNodes]: %s", err.Error())
        return ctx.String(http.StatusInternalServerError, err.Error())
    }
    tabletsFutures := map[string]chan helpers.TabletsFuture{}
    for _, host := range nodes {
        tabletsFuture := make(chan helpers.TabletsFuture)
        tabletsFutures[host] = tabletsFuture
        go c.helper.GetTabletsFuture(host, tabletsFuture)
    }
    for host, tabletsFuture := range tabletsFutures {
        c.logger.Debugf("getting tablets from tserver %s", host)
        tabletsList := <- tabletsFuture
        if tabletsList.Error != nil {
            c.logger.Warnf("[GetTabletsFuture] for node %s: %s", host, tabletsList.Error.Error())
            continue
        }
        for tabletId, tabletInfo := range tabletsList.Tablets {
            // skip if tablet already in map
            if _, ok := tabletListResponse.Data[tabletId]; ok {
                c.logger.Debugf("skipping tablet id %s", tabletId)
                continue
            }
            c.logger.Debugf("adding tablet id %s", tabletId)
            hasLeader := false
            for _, obj := range tabletInfo.RaftConfig {
                if _, ok := obj["LEADER"]; ok {
                    hasLeader = true
                    break
                }
            }
            tabletListResponse.Data[tabletId] = models.ClusterTablet{
                Namespace: tabletInfo.Namespace,
                TableName: tabletInfo.TableName,
                TableUuid: tabletInfo.TableId,
                TabletId:  tabletId,
                HasLeader: hasLeader,
            }
        }
    }
    return ctx.JSON(http.StatusOK, tabletListResponse)
}

// GetVersion - Get YugabyteDB version
func (c *Container) GetVersion(ctx echo.Context) error {
    tabletServersFuture := make(chan helpers.TabletServersFuture)
    masterAddressesFuture := make(chan helpers.MasterAddressesFuture)
    go c.helper.GetTabletServersFuture(helpers.HOST, tabletServersFuture)
    go c.helper.GetMasterAddressesFuture(masterAddressesFuture)

    // Get response from tabletServersFuture
    tabletServersResponse := <-tabletServersFuture
    if tabletServersResponse.Error != nil {
        c.logger.Errorf("[GetTabletServersFuture]: %s", tabletServersResponse.Error.Error())
        return ctx.String(http.StatusInternalServerError,
            tabletServersResponse.Error.Error())
    }
    // Get response from masterAddressesFuture
    masterAddressesResponse := <-masterAddressesFuture
    if masterAddressesResponse.Error != nil {
        c.logger.Errorf("failed to get master addresses")
        return ctx.String(http.StatusInternalServerError,
            masterAddressesResponse.Error.Error())
    }

    // List of tservers
    nodeList := c.helper.GetNodesList(tabletServersResponse)
    // List of masters
    masterList := masterAddressesResponse.HostList
    versionInfoFutures := map[string]chan helpers.VersionInfoFuture{}
    for _, nodeHost := range nodeList {
        versionInfoFuture := make(chan helpers.VersionInfoFuture)
        versionInfoFutures[nodeHost] = versionInfoFuture
        go c.helper.GetVersionFuture(nodeHost, false, versionInfoFuture)
    }
    versionInfoMasterFutures := map[string]chan helpers.VersionInfoFuture{}
    for _, masterHost := range masterList {
        versionInfoFuture := make(chan helpers.VersionInfoFuture)
        versionInfoMasterFutures[masterHost] = versionInfoFuture
        go c.helper.GetVersionFuture(masterHost, true, versionInfoFuture)
    }
    // We return the smallest version of all masters/tservers
    smallestVersion := c.helper.GetSmallestVersion(versionInfoFutures)
    smallestVersionMaster := c.helper.GetSmallestVersion(versionInfoMasterFutures)
    if smallestVersion == "" ||
        c.helper.CompareVersions(smallestVersion, smallestVersionMaster) > 0 {
        smallestVersion = smallestVersionMaster
    }
    return ctx.JSON(http.StatusOK, models.VersionInfo{
        Version: smallestVersion,
    })
}

// GetIsLoadBalancerIdle - Check if cluster load balancer is idle
func (c *Container) GetIsLoadBalancerIdle(ctx echo.Context) error {
    mastersFuture := make(chan helpers.MastersFuture)
    go c.helper.GetMastersFuture(mastersFuture)
    masters := map[string]helpers.Master{}
    mastersResponse := <-mastersFuture
    // Build comma separated master addresses list for yb-admin
    csvMasterAddresses := ""
    if mastersResponse.Error != nil {
        c.logger.Errorf("[GetMastersFuture]: %s", mastersResponse.Error.Error())
        return ctx.String(http.StatusInternalServerError, mastersResponse.Error.Error())
    } else {
        for _, master := range mastersResponse.Masters {
            if len(master.Registration.PrivateRpcAddresses) > 0 {
                masters[master.Registration.PrivateRpcAddresses[0].Host] = master
                csvMasterAddresses += fmt.Sprintf(
                    "%s:%d,",
                    master.Registration.PrivateRpcAddresses[0].Host,
                    master.Registration.PrivateRpcAddresses[0].Port)
            }
        }
    }
    // Assume idle by default
    isLoadBalancerIdle := true
    params := []string{
        "--master_addresses",
        csvMasterAddresses,
        "get_is_load_balancer_idle",
    }
    loadBalancerIdleFuture := make(chan helpers.YBAdminFuture)
    go c.helper.RunYBAdminFuture(params, loadBalancerIdleFuture)
    loadBalancerResult := <-loadBalancerIdleFuture
    if loadBalancerResult.Error != nil {
        c.logger.Errorf("failed to get_is_load_balancer_idle result: %s",
            loadBalancerResult.Error.Error())
    } else {
        isLoadBalancerIdle = strings.Contains(loadBalancerResult.Result, "1")
    }
    return ctx.JSON(http.StatusOK, models.IsLoadBalancerIdle{
        IsIdle: isLoadBalancerIdle,
    })
}

// GetGflagsJson - Retrieve the gflags from Master and Tserver process
func (c *Container) GetGflagsJson(ctx echo.Context) error {

    nodeHost := ctx.QueryParam("node_address")
    if nodeHost == "" {
        nodeHost = helpers.HOST
    }

    gFlagsTserverFuture := make(chan helpers.GFlagsJsonFuture)
    go c.helper.GetGFlagsJsonFuture(nodeHost, false, gFlagsTserverFuture)
    gFlagsMasterFuture := make(chan helpers.GFlagsJsonFuture)
    go c.helper.GetGFlagsJsonFuture(nodeHost, true, gFlagsMasterFuture)

    masterFlags := <-gFlagsMasterFuture
    if masterFlags.Error != nil {
        c.logger.Warnf("failed to get master flags from %s: %s",
            nodeHost, masterFlags.Error.Error())
    }
    tserverFlags := <-gFlagsTserverFuture
    if tserverFlags.Error != nil {
        c.logger.Warnf("failed to get tserver flags from %s: %s",
            nodeHost, tserverFlags.Error.Error())
    }

    // Type conversion from helpers.GFlag to models.Gflag
    masterFlagsResponse := []models.Gflag{}
    for _, obj := range masterFlags.GFlags {
        masterFlagsResponse = append(masterFlagsResponse, models.Gflag(obj))
    }

    tserverFlagsResponse := []models.Gflag{}
    for _, obj := range tserverFlags.GFlags {
        tserverFlagsResponse = append(tserverFlagsResponse, models.Gflag(obj))
    }

    return ctx.JSON(http.StatusOK, models.GflagsInfo{
        MasterFlags:  masterFlagsResponse,
        TserverFlags: tserverFlagsResponse,
    })

}

// GetTableInfo - Get info on a single table, given table uuid
func (c *Container) GetTableInfo(ctx echo.Context) error {

    id := ctx.QueryParam("id")
    nodeHost := ctx.QueryParam("node_address")

    if id == "" {
        return ctx.String(http.StatusBadRequest, "Missing table id query parameter")
    }

    tableInfoFuture := make(chan helpers.TableInfoFuture)
    go c.helper.GetTableInfoFuture(nodeHost, id, tableInfoFuture)

    tableInfo := <- tableInfoFuture
    if tableInfo.Error != nil {
        c.logger.Errorf("[GetTableInfoFuture]: %s", tableInfo.Error.Error())
        return ctx.String(http.StatusInternalServerError, tableInfo.Error.Error())
    }

    // Get placement blocks for live replicas
    liveReplicaPlacementBlocks := []models.PlacementBlock{}
    for _, placementBlock :=
        range tableInfo.TableInfo.TableReplicationInfo.LiveReplicas.PlacementBlocks {
            liveReplicaPlacementBlocks = append(liveReplicaPlacementBlocks, models.PlacementBlock{
                CloudInfo: models.PlacementCloudInfo{
                    PlacementCloud: placementBlock.CloudInfo.PlacementCloud,
                    PlacementRegion: placementBlock.CloudInfo.PlacementRegion,
                    PlacementZone: placementBlock.CloudInfo.PlacementZone,
                },
                MinNumReplicas: int32(placementBlock.MinNumReplicas),
            })
    }

    // Get read replicas
    readReplicas := []models.TableReplicationInfo{}
    for _, readReplica :=
        range tableInfo.TableInfo.TableReplicationInfo.ReadReplicas {
            readReplicaInfo := models.TableReplicationInfo{}
            readReplicaInfo.NumReplicas = int32(readReplica.NumReplicas)
            for _, placementBlock := range readReplica.PlacementBlocks {
                readReplicaInfo.PlacementBlocks = append(readReplicaInfo.PlacementBlocks,
                    models.PlacementBlock{
                        CloudInfo: models.PlacementCloudInfo{
                            PlacementCloud: placementBlock.CloudInfo.PlacementCloud,
                            PlacementRegion: placementBlock.CloudInfo.PlacementRegion,
                            PlacementZone: placementBlock.CloudInfo.PlacementZone,
                        },
                        MinNumReplicas: int32(placementBlock.MinNumReplicas),
                    })
            }
            readReplicaInfo.PlacementUuid = readReplica.PlacementUuid
            readReplicas = append(readReplicas, readReplicaInfo)
    }

    // Get columns
    columns := []models.ColumnInfo{}
    for _, column := range tableInfo.TableInfo.Columns {
        columns = append(columns, models.ColumnInfo(column))
    }

    // Get tablets
    tablets := []models.TabletInfo{}
    for _, tablet := range tableInfo.TableInfo.Tablets {
        hidden, err := strconv.ParseBool(tablet.Hidden)
        // If parsebool fails, assume tablet is not hidden
        if err != nil {
            c.logger.Warnf("failed to parse if tablet is hidden")
            hidden = false
        }
        // Get Raft Config info
        raftConfig := []models.RaftConfig{}
        for _, location := range tablet.Locations {
            raftConfig = append(raftConfig, models.RaftConfig(location))
        }
        tablets = append(tablets, models.TabletInfo{
            TabletId: tablet.TabletId,
            Partition: tablet.Partition,
            SplitDepth: tablet.SplitDepth,
            State: tablet.State,
            Hidden: hidden,
            Message: tablet.Message,
            RaftConfig: raftConfig,
        })
    }

    return ctx.JSON(http.StatusOK, models.TableInfo{
        TableName: tableInfo.TableInfo.TableName,
        TableId: tableInfo.TableInfo.TableId,
        TableVersion: tableInfo.TableInfo.TableVersion,
        TableType: tableInfo.TableInfo.TableType,
        TableState: tableInfo.TableInfo.TableState,
        TableStateMessage: tableInfo.TableInfo.TableStateMessage,
        TableTablespaceOid: tableInfo.TableInfo.TableTablespaceOid,
        TableReplicationInfo: models.TableInfoTableReplicationInfo{
            LiveReplicas: models.TableReplicationInfo{
                NumReplicas:
                    int32(tableInfo.TableInfo.TableReplicationInfo.LiveReplicas.NumReplicas),
                PlacementBlocks: liveReplicaPlacementBlocks,
                PlacementUuid: tableInfo.TableInfo.TableReplicationInfo.LiveReplicas.PlacementUuid,
            },
            ReadReplicas: readReplicas,
        },
        Columns: columns,
        Tablets: tablets,
    })

}

// GetClusterAlerts - Get all cluster alerts info (If Any)
func (c *Container) GetClusterAlerts(ctx echo.Context) error {

    nodeHost := ctx.QueryParam("node_address")
    // If node_address is provided, get alerts from node at that address
    if nodeHost != "" {
        httpClient := &http.Client{
            Timeout: time.Second * 10,
        }
        url := fmt.Sprintf("http://%s:%s/api/alerts", nodeHost, c.serverPort)
        resp, err := httpClient.Get(url)
        if err != nil {
            c.logger.Errorf("Failed to get alerts from node %s: %s", nodeHost, err.Error())
            return ctx.String(http.StatusInternalServerError, err.Error())
        }
        defer resp.Body.Close()
        return ctx.Stream(resp.StatusCode, echo.MIMEApplicationJSONCharsetUTF8, resp.Body)
    }
    alertsResponse := models.AlertsResponse {
        Data: []models.AlertsInfo{},
    }

    if helpers.Warnings != "" {
        warnings := strings.Split(helpers.Warnings, "|")

        for _, warning := range warnings {
            alertsResponse.Data = append(alertsResponse.Data, models.AlertsInfo{
                Name: warning,
                Info: WARNING_MSGS[warning],
            })
        }
    }

    // Check for version mismatches among nodes
    nodeVersions, err := c.helper.GetAllNodeVersions()
    if err != nil {
        c.logger.Errorf("Error fetching node versions: %s", err.Error())
        return ctx.String(http.StatusInternalServerError, err.Error())
    }
    isVersionMismatch := c.helper.ValidateVersions(nodeVersions)
    if isVersionMismatch {
        var versionDetails []string
        for nodeIP, version := range nodeVersions {
            versionDetails = append(versionDetails, fmt.Sprintf("%s:%s", nodeIP, version))
        }
        mismatchInfo := fmt.Sprintf(
            "Node version mismatch detected. Following are the version of nodes: %s",
            strings.Join(versionDetails, ", "),
        )
        alertsResponse.Data = append(alertsResponse.Data, models.AlertsInfo{
            Name: "version mismatch",
            Info: mismatchInfo,
        })
    }
    return ctx.JSON(http.StatusOK, alertsResponse)
}

// GetClusterConnections - Get YSQL connection manager stats for every node of the cluster
func (c *Container) GetClusterConnections(ctx echo.Context) error {
    connectionsResponse := models.ConnectionsStats{
        Data: map[string][]models.ConnectionStatsItem{},
    }
    nodeList, err := c.getNodes()
    if err != nil {
        return ctx.String(http.StatusInternalServerError, err.Error())
    }
    connectionsFutures := map[string]chan helpers.ConnectionsFuture{}
    for _, nodeHost := range nodeList {
        connectionsFuture := make(chan helpers.ConnectionsFuture)
        connectionsFutures[nodeHost] = connectionsFuture
        go c.helper.GetConnectionsFuture(nodeHost, connectionsFuture)
    }
    for nodeHost, future := range connectionsFutures {
        connectionResponse := <-future
        if connectionResponse.Error != nil {
            c.logger.Errorf("failed to get connection stats from %s: %s",
                nodeHost, connectionResponse.Error.Error())
            continue
        }
        connectionsResponse.Data[nodeHost] = []models.ConnectionStatsItem{}
        for _, connectionPool := range connectionResponse.Pools {
            if connectionPool.DatabaseName != "control_connection" {
                connectionsResponse.Data[nodeHost] = append(
                    connectionsResponse.Data[nodeHost],
                    models.ConnectionStatsItem{
                        DatabaseName: connectionPool.DatabaseName,
                        UserName: connectionPool.UserName,
                        ActiveLogicalConnections: connectionPool.ActiveLogicalConnections,
                        QueuedLogicalConnections: connectionPool.QueuedLogicalConnections,
                        IdleOrPendingLogicalConnections:
                            connectionPool.IdleOrPendingLogicalConnections,
                        ActivePhysicalConnections: connectionPool.ActivePhysicalConnections,
                        IdlePhysicalConnections: connectionPool.IdlePhysicalConnections,
                        AvgWaitTimeNs: connectionPool.AvgWaitTimeNs,
                        Qps: connectionPool.Qps,
                        Tps: connectionPool.Tps,
                    },
                )
            }
        }
        if len(connectionsResponse.Data[nodeHost]) == 0 {
            c.logger.Errorf("did not find non-control connection pool info for %s", nodeHost)
        }
    }
    return ctx.JSON(http.StatusOK, connectionsResponse)
}

// GetNodeAddress - Get the node address for the current node
func (c *Container) GetNodeAddress(ctx echo.Context) error {
    return ctx.String(http.StatusOK, helpers.HOST)
}
