package handlers

import (
    "apiserver/cmd/server/helpers"
    "apiserver/cmd/server/models"
    "context"
    "encoding/json"
    "fmt"
    "math"
    "net"
    "net/http"
    "sort"
    "strconv"
    "strings"
    "time"

    "github.com/jackc/pgx/v4"
    "github.com/labstack/echo/v4"
    "github.com/yugabyte/gocql"
)

const SLOW_QUERY_STATS_SQL string = "SELECT a.rolname, t.datname, t.queryid, " +
    "t.query, t.calls, t.total_time, t.rows, t.min_time, t.max_time, t.mean_time, t.stddev_time, " +
    "t.local_blks_hit, t.local_blks_written FROM pg_authid a JOIN (SELECT * FROM " +
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

type SlowQueriesFuture struct {
    Items []*models.SlowQueryResponseYsqlQueryItem
    Error error
}

type DetailObj struct {
    Value float64 `json:"value"`
}

// return hostname of each node
func getNodes() ([]string, error) {
    hostNames := []string{}
    tabletServersFuture := make(chan helpers.TabletServersFuture)
    go helpers.GetTabletServersFuture(helpers.HOST, tabletServersFuture)
    tabletServersResponse := <-tabletServersFuture
    if tabletServersResponse.Error != nil {
        return hostNames, tabletServersResponse.Error
    }
    // to get hostnames, get all second level keys and only keep them if net.SpliHostPort succeeds.
    for _, obj := range tabletServersResponse.Tablets {
        for hostport := range obj {
            host, _, err := net.SplitHostPort(hostport)
            if err == nil {
                hostNames = append(hostNames, host)
            }
        }
    }
    return hostNames, nil
}

func getSlowQueriesFuture(nodeHost string, future chan SlowQueriesFuture) {
    slowQueries := SlowQueriesFuture{
        Items: []*models.SlowQueryResponseYsqlQueryItem{},
        Error: nil,
    }
    url := fmt.Sprintf("postgres://%s:%s@%s:%d/%s",
        helpers.USER, helpers.PASSWORD, nodeHost, helpers.PORT, helpers.DBNAME)
    conn, err := pgx.Connect(context.Background(), url)
    if err != nil {
        slowQueries.Error = err
        future <- slowQueries
        return
    }
    defer conn.Close(context.Background())

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
            &rowStruct.MinTime, &rowStruct.MaxTime, &rowStruct.MeanTime, &rowStruct.StddevTime,
            &rowStruct.LocalBlksHit, &rowStruct.LocalBlksWritten)
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
            if len(nodeValuesNumerator[i][j]) < 2 || len(nodeValuesDenominator[i][j]) < 2 {
                // Handle case where data at window is empty
                resultMetric[i][j] = []float64{nodeValuesNumerator[i][j][0]}
            } else if nodeValuesDenominator[i][j][1] != 0 {
                // Handle divide by 0 case
                // Note: we are comparing a float to 0 to avoid dividing by 0.
                // This will only catch the cases where the float value is exactly 0.
                resultMetric[i][j] = []float64{nodeValuesNumerator[i][j][0],
                    nodeValuesNumerator[i][j][1] / nodeValuesDenominator[i][j][1]}
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
func getAveragePercentageMetricData(
    metricColumnValue string,
    nodeList []string,
    hostToUuid map[string]string,
    startTime int64,
    endTime int64,
    session *gocql.Session,
    detailsValue bool,
) ([][]float64, error) {
    metricValues := [][]float64{}
    rawMetricValues, err := getRawMetricsForAllNodes(metricColumnValue, nodeList, hostToUuid,
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
                newValues[newValuesIndex][1] = newValues[newValuesIndex][1] / float64(counter)
            } else if counter == 0 {
                // if no data for this interval, make it so that this timestamp has no value
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
        newNodeValues[i] = reduceGranularity(startTime, endTime, nodeValues[i], numIntervals,
            isAverage)
    }
    return newNodeValues
}

// Gets raw metrics for all provided nodes. Timestamps are returned in seconds.
func getRawMetricsForAllNodes(
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
                values = append(values, []float64{float64(ts) / 1000, detailObj.Value})
            } else {
                values = append(values, []float64{float64(ts) / 1000, float64(value)})
            }
        }
        if err := iter.Close(); err != nil {
            return nodeValues, err
        }
        sort.Slice(values, func(i, j int) bool {
            return values[i][0] < values[j][0]
        })
        nodeValues = append(nodeValues, values)
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

// GetBulkClusterMetrics - Get bulk cluster metrics
func (c *Container) GetBulkClusterMetrics(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld{
        Message: "Hello World",
    })
}

// GetClusterMetric - Get a metric for a cluster
func (c *Container) GetClusterMetric(ctx echo.Context) error {
    metricsParam := strings.Split(ctx.QueryParam("metrics"), ",")
    nodeParam := ctx.QueryParam("node_name")
    nodeList := []string{nodeParam}
    var err error = nil
    if nodeParam == "" {
        nodeList, err = getNodes()
        if err != nil {
            return ctx.String(http.StatusInternalServerError, err.Error())
        }
    }
    hostToUuid, err := helpers.GetHostToUuidMap(helpers.HOST)
    if err != nil {
        return ctx.String(http.StatusInternalServerError, err.Error())
    }
    // in case of errors parsing start/end time, set to defaults of start = 1 hour ago, end = now
    startTime, err := strconv.ParseInt(ctx.QueryParam("start_time"), 10, 64)
    if err != nil {
        now := time.Now()
        startTime = now.Unix()
    }
    endTime, err := strconv.ParseInt(ctx.QueryParam("end_time"), 10, 64)
    if err != nil {
        now := time.Now()
        endTime = now.Unix() - 60*60
    }

    metricResponse := models.MetricResponse{
        Data:           []models.MetricData{},
        StartTimestamp: startTime,
        EndTimestamp:   endTime,
    }

    cluster := gocql.NewCluster(helpers.HOST)

    // Use the same timeout as the Java driver.
    cluster.Timeout = 12 * time.Second

    // Create the session.
    session, err := cluster.CreateSession()
    if err != nil {
        return ctx.String(http.StatusInternalServerError, err.Error())
    }
    defer session.Close()

    for _, metric := range metricsParam {
        // Read from the table.
        var ts int64
        var value int
        var details string
        // need node uuid
        switch metric {
        case "READ_OPS_PER_SEC":
            rawMetricValues, err := getRawMetricsForAllNodes(READ_COUNT_METRIC, nodeList,
                hostToUuid, startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }
            rateMetrics := convertRawMetricsToRates(rawMetricValues)
            nodeMetricValues := reduceGranularityForAllNodes(startTime, endTime, rateMetrics,
                GRANULARITY_NUM_INTERVALS, true)
            metricValues := calculateCombinedMetric(nodeMetricValues, false)
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name:   metric,
                Values: metricValues,
            })
        case "WRITE_OPS_PER_SEC":
            rawMetricValues, err := getRawMetricsForAllNodes(WRITE_COUNT_METRIC, nodeList,
                hostToUuid, startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }
            rateMetrics := convertRawMetricsToRates(rawMetricValues)
            nodeMetricValues := reduceGranularityForAllNodes(startTime, endTime, rateMetrics,
                GRANULARITY_NUM_INTERVALS, true)
            metricValues := calculateCombinedMetric(nodeMetricValues, false)
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name:   metric,
                Values: metricValues,
            })
        case "CPU_USAGE_USER":
            metricValues, err := getAveragePercentageMetricData("cpu_usage_user", nodeList,
                hostToUuid, startTime, endTime, session, true)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name:   metric,
                Values: metricValues,
            })
        case "CPU_USAGE_SYSTEM":
            metricValues, err := getAveragePercentageMetricData("cpu_usage_system", nodeList,
                hostToUuid, startTime, endTime, session, true)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name:   metric,
                Values: metricValues,
            })
        case "DISK_USAGE_GB":
            // For disk usage, we assume every node reports the same metrics
            query := fmt.Sprintf(QUERY_FORMAT, "system.metrics", "total_disk", startTime*1000,
                endTime*1000)
            iter := session.Query(query).Iter()
            values := [][]float64{}
            for iter.Scan(&ts, &value, &details) {
                values = append(values,
                    []float64{float64(ts) / 1000, float64(value) / helpers.BYTES_IN_GB})
            }
            if err := iter.Close(); err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }
            sort.Slice(values, func(i, j int) bool {
                return values[i][0] < values[j][0]
            })
            query = fmt.Sprintf(QUERY_FORMAT, "system.metrics", "free_disk", startTime*1000,
                endTime*1000)
            iter = session.Query(query).Iter()
            freeValues := [][]float64{}
            for iter.Scan(&ts, &value, &details) {
                freeValues = append(freeValues,
                    []float64{float64(ts) / 1000, float64(value) / helpers.BYTES_IN_GB})
            }
            if err := iter.Close(); err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }
            sort.Slice(freeValues, func(i, j int) bool {
                return freeValues[i][0] < freeValues[j][0]
            })

            // we assume the query results for free and total disk have the same timestamps
            for index, pair := range freeValues {
                if index >= len(values) {
                    break
                }
                values[index][1] -= float64(pair[1])
            }
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name:   metric,
                Values: reduceGranularity(startTime, endTime, values, GRANULARITY_NUM_INTERVALS,
                    true),
            })
        case "PROVISIONED_DISK_SPACE_GB":
            query := fmt.Sprintf(QUERY_FORMAT, "system.metrics", "total_disk", startTime*1000,
                endTime*1000)
            iter := session.Query(query).Iter()
            values := [][]float64{}
            for iter.Scan(&ts, &value, &details) {
                values = append(values,
                    []float64{float64(ts) / 1000, float64(value) / helpers.BYTES_IN_GB})
            }
            if err := iter.Close(); err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }
            sort.Slice(values, func(i, j int) bool {
                return values[i][0] < values[j][0]
            })
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name:   metric,
                Values: reduceGranularity(startTime, endTime, values, GRANULARITY_NUM_INTERVALS,
                    true),
            })
        case "AVERAGE_READ_LATENCY_MS":
            rawMetricValuesCount, err := getRawMetricsForAllNodes(READ_COUNT_METRIC, nodeList,
                hostToUuid, startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }

            rawMetricValuesSum, err := getRawMetricsForAllNodes(READ_SUM_METRIC, nodeList,
                hostToUuid, startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }

            rateMetricsCount := convertRawMetricsToRates(rawMetricValuesCount)
            rateMetricsSum := convertRawMetricsToRates(rawMetricValuesSum)

            rateMetricsCountReduced := reduceGranularityForAllNodes(startTime, endTime,
                rateMetricsCount, GRANULARITY_NUM_INTERVALS, false)

            rateMetricsSumReduced := reduceGranularityForAllNodes(startTime, endTime,
                rateMetricsSum, GRANULARITY_NUM_INTERVALS, false)

            rateMetricsCountCombined := calculateCombinedMetric(rateMetricsCountReduced, false)
            rateMetricsSumCombined := calculateCombinedMetric(rateMetricsSumReduced, false)

            latencyMetric := divideMetricForAllNodes([][][]float64{rateMetricsSumCombined},
                [][][]float64{rateMetricsCountCombined})

            metricValues := latencyMetric[0]
            // Divide everything by 1000 to convert from microseconds to milliseconds
            divideMetricByConstant(metricValues, 1000)
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name:   metric,
                Values: metricValues,
            })
        case "AVERAGE_WRITE_LATENCY_MS":
            rawMetricValuesCount, err := getRawMetricsForAllNodes(WRITE_COUNT_METRIC, nodeList,
                hostToUuid, startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }

            rawMetricValuesSum, err := getRawMetricsForAllNodes(WRITE_SUM_METRIC, nodeList,
                hostToUuid, startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }

            rateMetricsCount := convertRawMetricsToRates(rawMetricValuesCount)
            rateMetricsSum := convertRawMetricsToRates(rawMetricValuesSum)

            rateMetricsCountReduced := reduceGranularityForAllNodes(startTime, endTime,
                rateMetricsCount, GRANULARITY_NUM_INTERVALS, false)

            rateMetricsSumReduced := reduceGranularityForAllNodes(startTime, endTime,
                rateMetricsSum, GRANULARITY_NUM_INTERVALS, false)

            rateMetricsCountCombined := calculateCombinedMetric(rateMetricsCountReduced, false)
            rateMetricsSumCombined := calculateCombinedMetric(rateMetricsSumReduced, false)

            latencyMetric := divideMetricForAllNodes([][][]float64{rateMetricsSumCombined},
                [][][]float64{rateMetricsCountCombined})

            metricValues := latencyMetric[0]
            // Divide everything by 1000 to convert from microseconds to milliseconds
            divideMetricByConstant(metricValues, 1000)
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name:   metric,
                Values: metricValues,
            })
        case "TOTAL_LIVE_NODES":
            rawMetricValues, err := getRawMetricsForAllNodes("node_up", nodeList, hostToUuid,
                startTime, endTime, session, false)
            if err != nil {
                return ctx.String(http.StatusInternalServerError, err.Error())
            }
            reducedMetric := reduceGranularityForAllNodes(startTime, endTime, rawMetricValues,
                GRANULARITY_NUM_INTERVALS, true)
            metricValues := calculateCombinedMetric(reducedMetric, false)
            // In cases where there is no data, set to 0
            for i, metric := range metricValues {
                if len(metric) < 2 {
                    metricValues[i] = append(metricValues[i], 0)
                }
            }
            metricResponse.Data = append(metricResponse.Data, models.MetricData{
                Name: metric,
                Values: metricValues,
            })
        }
    }
    return ctx.JSON(http.StatusOK, metricResponse)
}

// GetClusterNodes - Get the nodes for a cluster
func (c *Container) GetClusterNodes(ctx echo.Context) error {
    response := models.ClusterNodesResponse{
        Data: []models.NodeData{},
    }
    tabletServersFuture := make(chan helpers.TabletServersFuture)
    go helpers.GetTabletServersFuture(helpers.HOST, tabletServersFuture)
    tabletServersResponse := <-tabletServersFuture
    if tabletServersResponse.Error != nil {
        return ctx.String(http.StatusInternalServerError, tabletServersResponse.Error.Error())
    }
    nodeList := helpers.GetNodesList(tabletServersResponse)
    versionInfoFutures := map[string]chan helpers.VersionInfoFuture{}
    for _, nodeHost := range nodeList {
        versionInfoFuture := make(chan helpers.VersionInfoFuture)
        versionInfoFutures[nodeHost] = versionInfoFuture
        go helpers.GetVersionFuture(nodeHost, versionInfoFuture)
    }
    for _, obj := range tabletServersResponse.Tablets {
        for hostport, nodeData := range obj {
            host, _, err := net.SplitHostPort(hostport)
            // If we can split hostport, just use host as name.
            // Otherwise, use hostport as name.
            // However, we can only get version information if we can get the host
            hostName := hostport
            versionNumber := ""
            if err == nil {
                hostName = host
                versionInfo := <-versionInfoFutures[hostName]
                if versionInfo.Error == nil {
                    versionNumber = versionInfo.VersionInfo.VersionNumber
                }
            }
            totalSstFileSizeBytes := int64(nodeData.TotalSstFileSizeBytes)
            uncompressedSstFileSizeBytes :=
                int64(nodeData.UncompressedSstFileSizeBytes)
            // For now, assuming that IsMaster and IsTserver are always true
            // The UI frontend doesn't use these values so this should be ok for now
            response.Data = append(response.Data, models.NodeData{
                Name:      hostName,
                IsNodeUp:  nodeData.Status == "ALIVE",
                IsMaster:  true,
                IsTserver: true,
                Metrics: models.NodeDataMetrics{
                    MemoryUsedBytes:              int64(nodeData.RamUsedBytes),
                    TotalSstFileSizeBytes:        &totalSstFileSizeBytes,
                    UncompressedSstFileSizeBytes: &uncompressedSstFileSizeBytes,
                    ReadOpsPerSec:                nodeData.ReadOpsPerSec,
                    WriteOpsPerSec:               nodeData.WriteOpsPerSec,
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
    sort.Slice(response.Data, func(i, j int) bool {
        return response.Data[i].Name < response.Data[j].Name
    })
    return ctx.JSON(http.StatusOK, response)
}

// GetClusterTables - Get list of DB tables per YB API (YCQL/YSQL)
func (c *Container) GetClusterTables(ctx echo.Context) error {
    tableListResponse := models.ClusterTableListResponse{
        Data: []models.ClusterTable{},
    }
    tablesFuture := make(chan helpers.TablesFuture)
    go helpers.GetTablesFuture(helpers.HOST, tablesFuture)
    tablesList := <-tablesFuture
    if tablesList.Error != nil {
        return ctx.String(http.StatusInternalServerError, tablesList.Error.Error())
    }
    api := ctx.QueryParam("api")
    switch api {
    case "YSQL":
        for _, table := range tablesList.Tables {
            if table.IsYsql {
                tableListResponse.Data = append(tableListResponse.Data, models.ClusterTable{
                    Name: table.Name,
                    Keyspace: table.Keyspace,
                    Type: models.YBAPIENUM_YSQL,
                    SizeBytes: table.SizeBytes,
                })
            }
        }
    case "YCQL":
        for _, table := range tablesList.Tables {
            if !table.IsYsql {
                tableListResponse.Data = append(tableListResponse.Data, models.ClusterTable{
                    Name: table.Name,
                    Keyspace: table.Keyspace,
                    Type: models.YBAPIENUM_YCQL,
                    SizeBytes: table.SizeBytes,
                })
            }
        }
    }
    return ctx.JSON(http.StatusOK, tableListResponse)
}

// GetClusterTablespaces - Get list of DB tables for YSQL
func (c *Container) GetClusterTablespaces(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld{
        Message: "Hello World",
    })
}

// GetLiveQueries - Get the live queries in a cluster
func (c *Container) GetLiveQueries(ctx echo.Context) error {
    api := ctx.QueryParam("api")
    liveQueryResponse := models.LiveQueryResponseSchema{
        Data: models.LiveQueryResponseData{},
    }
    nodes, err := getNodes()
    if err != nil {
        return ctx.String(http.StatusInternalServerError, err.Error())
    }
    if api == "YSQL" {
        liveQueryResponse.Data.Ysql = models.LiveQueryResponseYsqlData{
            ErrorCount: 0,
            Queries:    []models.LiveQueryResponseYsqlQueryItem{},
        }
        // Get live queries of all nodes in parallel
        futures := []chan helpers.LiveQueriesYsqlFuture{}
        for _, nodeHost := range nodes {
            future := make(chan helpers.LiveQueriesYsqlFuture)
            futures = append(futures, future)
            go helpers.GetLiveQueriesYsqlFuture(nodeHost, future)
        }
        for _, future := range futures {
            items := <-future
            if items.Error != nil {
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
        futures := []chan helpers.LiveQueriesYcqlFuture{}
        for _, nodeHost := range nodes {
            future := make(chan helpers.LiveQueriesYcqlFuture)
            futures = append(futures, future)
            go helpers.GetLiveQueriesYcqlFuture(nodeHost, future)
        }
        for _, future := range futures {
            items := <-future
            if items.Error != nil {
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
    nodes, err := getNodes()
    if err != nil {
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
    futures := []chan SlowQueriesFuture{}
    for _, nodeHost := range nodes {
        future := make(chan SlowQueriesFuture)
        futures = append(futures, future)
        go getSlowQueriesFuture(nodeHost, future)
    }
    // Keep track of stats for each query so we can aggregrate the states over all nodes
    queryMap := map[string]*models.SlowQueryResponseYsqlQueryItem{}
    for _, future := range futures {
        items := <-future
        if items.Error != nil {
            slowQueryResponse.Data.Ysql.ErrorCount++
            continue
        }
        for _, item := range items.Items {
            if val, ok := queryMap[item.Query]; ok {
                // If the query is already in the map, we update its stats

                // item is new query, val is previous queries

                // Defining values to reuse.
                // The OpenAPI spec has these set to "number" which becomes float32 in go.
                // Since functions like math.Max, math.Min, math.Pow operate on float64 only,
                // we convert to float64 for calculation then convert to float32 for assignment.
                // We could set format: double in schemas/_index.yaml to make all the fields
                // into float64 to avoid conversion, or define our own math functions for float32.
                X_a := float64(val.MeanTime)
                X_b := float64(item.MeanTime)
                n_a := float64(val.Calls)
                n_b := float64(item.Calls)
                S_a := float64(val.StddevTime)
                S_b := float64(item.StddevTime)

                val.TotalTime += item.TotalTime
                val.Calls += item.Calls
                val.Rows += item.Rows
                val.MaxTime = float32(math.Max(float64(val.MaxTime), float64(item.MaxTime)))
                val.MinTime = float32(math.Min(float64(val.MinTime), float64(item.MinTime)))
                val.LocalBlksWritten += item.LocalBlksWritten
                /*
                 * Formula to calculate std dev of two samples: Let mean, std dev, and size of
                 * sample A be X_a, S_a, n_a respectively; and mean, std dev, and size of sample B
                 * be X_b, S_b, n_b respectively. Then mean of combined sample X is given by
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
                stdDevTime := math.Sqrt((n_a*(math.Pow(S_a, 2)+math.Pow(X_a-averageTime, 2)) +
                    n_b*(math.Pow(S_b, 2)+math.Pow(X_b-averageTime, 2))) /
                    totalCalls)
                val.MeanTime = float32(averageTime)
                val.StddevTime = float32(stdDevTime)
            } else {
                // If the query is not already in the map, add it to the map.
                queryMap[item.Query] = item
            }
        }
    }
    // put queries into slice and return
    for _, value := range queryMap {
        slowQueryResponse.Data.Ysql.Queries = append(slowQueryResponse.Data.Ysql.Queries, *value)
    }
    return ctx.JSON(http.StatusOK, slowQueryResponse)
}
