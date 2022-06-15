package handlers

import (
	"apiserver/cmd/server/models"
	"context"
	"crypto/rand"
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"math"
	"math/big"
	"net"
	"net/http"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/jackc/pgx/v4"
	"github.com/labstack/echo/v4"
	"github.com/yugabyte/gocql"
)

// hard coded
const (
	host     = "127.0.0.1"
	port     = 5433
	user     = "yugabyte"
	password = "yugabyte"
	dbname   = "yugabyte"
)

const SLOW_QUERY_STATS_SQL string = "SELECT a.rolname, t.datname, t.queryid, " +
	"t.query, t.calls, t.total_time, t.rows, t.min_time, t.max_time, t.mean_time, t.stddev_time, " +
	"t.local_blks_hit, t.local_blks_written FROM pg_authid a JOIN (SELECT * FROM " +
	"pg_stat_statements s JOIN pg_database d ON s.dbid = d.oid) t ON a.oid = t.userid"

var EXCLUDED_QUERY_STATEMENTS = map[string]bool{
	"SET extra_float_digits = 3": true,
	SLOW_QUERY_STATS_SQL:         true,
}

const BYTES_IN_GB = 1024 * 1024 * 1024

const QUERY_FORMAT_NODE string = "select ts, value, details from %s where metric = '%s' and node = '%s' and ts >= %d and ts < %d"

// for now, we always aggregate over all nodes
const QUERY_FORMAT string = "select ts, value, details from %s where metric = '%s' and ts >= %d and ts < %d"

const GRANULARITY_NUM_INTERVALS = 120

type LiveQueryHttpYsqlResponseConnection struct {
	BackendType    string `json:"backend_type"`
	DbName         string `json:"db_name"`
	SessionStatus  string `json:"backend_status"`
	Query          string `json:"query"`
	ElapsedMillis  int64  `json:"query_running_for_ms"`
	QueryStartTime string `json:"query_start_time"`
	AppName        string `json:"application_name"`
	ClientHost     string `json:"host"`
	ClientPort     string `json:"port"`
}

type LiveQueryHttpYcqlResponseCallDetails struct {
	SqlString string `json:"sql_string"`
}
type LiveQueryHttpYcqlResponseCqlDetails struct {
	Type        string                                  `json:"type"`
	CallDetails []*LiveQueryHttpYcqlResponseCallDetails `json:"call_details"`
}

type LiveQueryHttpYcqlResponseQuery struct {
	CqlDetails     *LiveQueryHttpYcqlResponseCqlDetails `json:"cql_details"`
	EllapsedMillis int64                                `json:"elapsed_millis"`
}

type LiveQueryHttpYcqlCqlConnectionDetails struct {
	Keyspace string `json:"keyspace"`
}

type LiveQueryHttpYcqlConnectionDetails struct {
	CqlConnectionDetails LiveQueryHttpYcqlCqlConnectionDetails `json:"cql_connection_details"`
}

type LiveQueryHttpYcqlResponseInboundConnection struct {
	CallsInFlight     []*LiveQueryHttpYcqlResponseQuery  `json:"calls_in_flight"`
	ConnectionDetails LiveQueryHttpYcqlConnectionDetails `json:"connection_details"`
	RemoteIp          string                             `json:"remote_ip"`
}

type LiveQueryHttpYsqlResponse struct {
	Connections []*LiveQueryHttpYsqlResponseConnection `json:"connections"`
	Error       string                                 `json:"error"`
}

type LiveQueryHttpYcqlResponse struct {
	InboundConnections []*LiveQueryHttpYcqlResponseInboundConnection `json:"inbound_connections"`
	Error              string                                        `json:"error"`
}

type SlowQueriesFuture struct {
	Items []*models.SlowQueryResponseYsqlQueryItem
	Error error
}

type LiveQueriesYsqlFuture struct {
	Items []*models.LiveQueryResponseYsqlQueryItem
	Error error
}

type LiveQueriesYcqlFuture struct {
	Items []*models.LiveQueryResponseYcqlQueryItem
	Error error
}

type DetailObj struct {
	Value float64 `json:"value"`
}

func random128BitString() (string, error) {
	//Max random value, a 128-bits integer, i.e 2^128 - 1
	max := new(big.Int)
	max.Exp(big.NewInt(2), big.NewInt(128), nil).Sub(max, big.NewInt(1))

	//Generate cryptographically strong pseudo-random between 0 - max
	n, err := rand.Int(rand.Reader, max)
	if err != nil {
		return "", err
	}

	//String representation of n in hex
	nonce := n.Text(16)

	return nonce, nil
}

// return hostname of each node
func getNodes() ([]string, error) {
	hostNames := []string{}
	httpClient := &http.Client{
		Timeout: time.Second * 10,
	}
	url := fmt.Sprintf("http://%s:7000/api/v1/tablet-servers", host)
	resp, err := httpClient.Get(url)
	if err != nil {
		return hostNames, err
	}
	defer resp.Body.Close()
	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return hostNames, err
	}
	var result map[string]interface{}
	json.Unmarshal([]byte(body), &result)

	if val, ok := result["error"]; ok {
		return hostNames, errors.New(val.(string))
	}
	// an example for what result looks like:
	/*
		{
			"":
			{
				"127.0.0.3:9000":
				{
					...
				},
				"127.0.0.2:9000":
				{
					...
				},
				"127.0.0.1:9000":
				{
					...
				}
			}
		}
	*/
	// to get hostnames, we get all second level keys and only keep them if net.SpliHostPort succeeds.
	for _, obj := range result {
		for hostport := range obj.(map[string]interface{}) {
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
		user, password, nodeHost, port, dbname)
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
		err := rows.Scan(&rowStruct.Rolname, &rowStruct.Datname, &rowStruct.Queryid, &rowStruct.Query,
			&rowStruct.Calls, &rowStruct.TotalTime, &rowStruct.Rows, &rowStruct.MinTime, &rowStruct.MaxTime,
			&rowStruct.MeanTime, &rowStruct.StddevTime, &rowStruct.LocalBlksHit, &rowStruct.LocalBlksWritten)
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

func getLiveQueriesYsqlFuture(nodeHost string, future chan LiveQueriesYsqlFuture) {
	liveQueries := LiveQueriesYsqlFuture{
		Items: []*models.LiveQueryResponseYsqlQueryItem{},
		Error: nil,
	}
	httpClient := &http.Client{
		Timeout: time.Second * 10,
	}
	url := fmt.Sprintf("http://%s:13000/rpcz", nodeHost)
	resp, err := httpClient.Get(url)
	if err != nil {
		liveQueries.Error = err
		future <- liveQueries
		return
	}
	defer resp.Body.Close()
	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		liveQueries.Error = err
		future <- liveQueries
		return
	}
	var ysqlResponse LiveQueryHttpYsqlResponse
	json.Unmarshal([]byte(body), &ysqlResponse)
	for _, connection := range ysqlResponse.Connections {
		if connection.BackendType != "" && connection.SessionStatus != "" &&
			connection.BackendType == "client backend" && connection.SessionStatus != "idle" {
			// uuid is just a random number, it is used in the frontend as a table row key.
			uuid, err := random128BitString()
			if err != nil {
				liveQueries.Error = err
				future <- liveQueries
				return
			}
			// for now NodeName is the node host
			connectionToAdd := models.LiveQueryResponseYsqlQueryItem{
				Id:             uuid,
				NodeName:       nodeHost,
				DbName:         connection.DbName,
				SessionStatus:  connection.SessionStatus,
				Query:          connection.Query,
				ElapsedMillis:  connection.ElapsedMillis,
				QueryStartTime: connection.QueryStartTime,
				AppName:        connection.AppName,
				ClientHost:     connection.ClientHost,
				ClientPort:     connection.ClientPort,
			}
			liveQueries.Items = append(liveQueries.Items, &connectionToAdd)
		}
	}
	future <- liveQueries
}

func getLiveQueriesYcqlFuture(nodeHost string, future chan LiveQueriesYcqlFuture) {
	liveQueries := LiveQueriesYcqlFuture{
		Items: []*models.LiveQueryResponseYcqlQueryItem{},
		Error: nil,
	}
	httpClient := &http.Client{
		Timeout: time.Second * 10,
	}
	url := fmt.Sprintf("http://%s:12000/rpcz", nodeHost)
	resp, err := httpClient.Get(url)
	if err != nil {
		liveQueries.Error = err
		future <- liveQueries
		return
	}
	defer resp.Body.Close()
	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		liveQueries.Error = err
		future <- liveQueries
		return
	}
	var ycqlResponse LiveQueryHttpYcqlResponse
	json.Unmarshal([]byte(body), &ycqlResponse)
	for _, inboundConnection := range ycqlResponse.InboundConnections {
		if inboundConnection.CallsInFlight != nil {
			for _, query := range inboundConnection.CallsInFlight {
				if query.CqlDetails != nil {
					var builder strings.Builder
					for _, callDetail := range query.CqlDetails.CallDetails {
						if builder.Len() > 0 {
							builder.WriteString(" ")
						}
						builder.WriteString(callDetail.SqlString)
					}
					clientHost, clientPort, err := net.SplitHostPort(inboundConnection.RemoteIp)
					if err != nil {
						// If we fail to get host and port, we set clientHost = RemoteIp
						clientHost = inboundConnection.RemoteIp
						clientPort = ""
					}
					// uuid is just a random number, it is used in the frontend as a table row key.
					uuid, err := random128BitString()
					if err != nil {
						liveQueries.Error = err
						future <- liveQueries
						return
					}
					// for now NodeName is the node host
					connectionToAdd := models.LiveQueryResponseYcqlQueryItem{
						Id:            uuid,
						NodeName:      nodeHost,
						Keyspace:      inboundConnection.ConnectionDetails.CqlConnectionDetails.Keyspace,
						Query:         builder.String(),
						Type:          query.CqlDetails.Type,
						ElapsedMillis: query.EllapsedMillis,
						ClientHost:    clientHost,
						ClientPort:    clientPort,
					}
					liveQueries.Items = append(liveQueries.Items, &connectionToAdd)
				}
			}
		}
	}
	future <- liveQueries
}

// For now, we hit the /tablet-servers endpoint and parse the html
func getHostToUuidMap(nodeHost string) (map[string]string, error) {
	hostToUuidMap := map[string]string{}
	httpClient := &http.Client{
		Timeout: time.Second * 10,
	}
	url := fmt.Sprintf("http://%s:7000/tablet-servers", host)
	resp, err := httpClient.Get(url)
	if err != nil {
		return hostToUuidMap, err
	}
	defer resp.Body.Close()
	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return hostToUuidMap, err
	}
	// Now we parse the html to get the hostnames and uuids
	regex := regexp.MustCompile(`<\/tr>\s*<tr>\s*<td><a href=.*?>(.*?)<\/a><\/br>\s*(.*?)<\/td>`)
	matches := regex.FindAllSubmatch(body, -1)
	for _, v := range matches {
		host, _, err := net.SplitHostPort(string(v[1]))
		if err != nil {
			return hostToUuidMap, err
		}
		hostToUuidMap[host] = string(v[2])
	}
	return hostToUuidMap, nil
}

// Divides each entry of nodeValuesNumerator by nodeValuesDenominator.
// Assumes that they are the same size, each node is listed in the same order, and their timestamps match up.
func divideMetricForAllNodes(nodeValuesNumerator [][][]float64, nodeValuesDenominator [][][]float64) [][][]float64 {
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
			// Note: we are comparing a float to 0 to avoid dividing by 0. This will only catch the cases where the
			// float value is exactly 0.
			if nodeValuesDenominator[i][j][1] != 0 {
				resultMetric[i][j] = []float64{nodeValuesNumerator[i][j][0], nodeValuesNumerator[i][j][1] / nodeValuesDenominator[i][j][1]}
			} else {
				resultMetric[i][j] = []float64{nodeValuesNumerator[i][j][0], 0}
			}
		}
	}
	return resultMetric
}

func divideMetric(valuesNumerator [][]float64, valuesDenominator [][]float64) [][]float64 {
	numIntervals := len(valuesNumerator)
	if len(valuesDenominator) < numIntervals {
		numIntervals = len(valuesDenominator)
	}
	resultMetric := make([][]float64, numIntervals)
	for i := 0; i < numIntervals; i++ {
		// In order to perform division, we need to check that the numerator value and denominator value both exist,
		// and that the denominator is not zero
		// In the case that either value does not exist (i.e. missing data for that interval) we leave result value empty
		// In the case that denominator is zero, set the value to zero.
		if len(valuesNumerator[i]) >= 2 && len(valuesDenominator[i]) >= 2 {
			if valuesDenominator[i][1] != 0 {
				resultMetric[i] = []float64{valuesNumerator[i][0], valuesNumerator[i][1] / valuesDenominator[i][1]}
			} else {
				resultMetric[i] = []float64{valuesNumerator[i][0], 0}
			}
		} else {
			resultMetric[i] = []float64{valuesNumerator[i][0]}
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

// Get metrics that are meant to be averaged over all nodes. detailsValue is true if the value of the metric is stored in
// the details column instead of the value column in the system.metrics table.
// Note: assumes values are percentages, and so all values are multiplied by 100
func getAveragePercentageMetricData(metricColumnValue string, nodeList []string, hostToUuid map[string]string, startTime int64, endTime int64, session *gocql.Session, detailsValue bool) ([][]float64, error) {
	metricValues := [][]float64{}
	rawMetricValues, err := getRawMetricsForAllNodes(metricColumnValue, nodeList, hostToUuid, startTime, endTime, session, detailsValue)
	if err != nil {
		return metricValues, err
	}
	nodeValues := reduceGranularityForAllNodes(startTime, endTime, rawMetricValues, GRANULARITY_NUM_INTERVALS, true)
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
func reduceGranularity(startTime int64, endTime int64, values [][]float64, numIntervals int, isAverage bool) [][]float64 {
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
				// if there was no data for this interval, make it so that this timestamp has no value
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

func reduceGranularityForAllNodes(startTime int64, endTime int64, nodeValues [][][]float64, numIntervals int, isAverage bool) [][][]float64 {
	newNodeValues := make([][][]float64, len(nodeValues))
	for i := 0; i < len(nodeValues); i++ {
		newNodeValues[i] = reduceGranularity(startTime, endTime, nodeValues[i], numIntervals, isAverage)
	}
	return newNodeValues
}

// Gets raw metrics for all provided nodes. Timestamps are returned in seconds.
func getRawMetricsForAllNodes(metricColumnValue string, nodeList []string, hostToUuid map[string]string, startTime int64, endTime int64, session *gocql.Session, detailsValue bool) ([][][]float64, error) {
	nodeValues := [][][]float64{}
	var ts int64
	var value int
	var details string
	for _, hostName := range nodeList {
		query := fmt.Sprintf(QUERY_FORMAT_NODE, "system.metrics", metricColumnValue, hostToUuid[hostName], startTime*1000, endTime*1000)
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
				[]float64{nodeValues[i][j][0], (nodeValues[i][j+1][1] - nodeValues[i][j][1]) / (nodeValues[i][j+1][0] - nodeValues[i][j][0])})
		}
		rateMetrics = append(rateMetrics, currentNodeValue)
	}
	return rateMetrics
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
	hostToUuid, err := getHostToUuidMap(host)
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

	cluster := gocql.NewCluster(host)

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
			rawMetricValues, err := getRawMetricsForAllNodes("handler_latency_yb_tserver_TabletServerService_Read_count", nodeList, hostToUuid, startTime, endTime, session, false)
			if err != nil {
				return ctx.String(http.StatusInternalServerError, err.Error())
			}
			rateMetrics := convertRawMetricsToRates(rawMetricValues)
			nodeMetricValues := reduceGranularityForAllNodes(startTime, endTime, rateMetrics, GRANULARITY_NUM_INTERVALS, true)
			metricValues := calculateCombinedMetric(nodeMetricValues, false)
			metricResponse.Data = append(metricResponse.Data, models.MetricData{
				Name:   metric,
				Values: metricValues,
			})
		case "WRITE_OPS_PER_SEC":
			rawMetricValues, err := getRawMetricsForAllNodes("handler_latency_yb_tserver_TabletServerService_Write_count", nodeList, hostToUuid, startTime, endTime, session, false)
			if err != nil {
				return ctx.String(http.StatusInternalServerError, err.Error())
			}
			rateMetrics := convertRawMetricsToRates(rawMetricValues)
			nodeMetricValues := reduceGranularityForAllNodes(startTime, endTime, rateMetrics, GRANULARITY_NUM_INTERVALS, true)
			metricValues := calculateCombinedMetric(nodeMetricValues, false)
			metricResponse.Data = append(metricResponse.Data, models.MetricData{
				Name:   metric,
				Values: metricValues,
			})
		case "CPU_USAGE_USER":
			metricValues, err := getAveragePercentageMetricData("cpu_usage_user", nodeList, hostToUuid, startTime, endTime, session, true)
			if err != nil {
				return ctx.String(http.StatusInternalServerError, err.Error())
			}
			metricResponse.Data = append(metricResponse.Data, models.MetricData{
				Name:   metric,
				Values: metricValues,
			})
		case "CPU_USAGE_SYSTEM":
			metricValues, err := getAveragePercentageMetricData("cpu_usage_system", nodeList, hostToUuid, startTime, endTime, session, true)
			if err != nil {
				return ctx.String(http.StatusInternalServerError, err.Error())
			}
			metricResponse.Data = append(metricResponse.Data, models.MetricData{
				Name:   metric,
				Values: metricValues,
			})
		case "DISK_USAGE_GB":
			// For disk usage, we assume every node reports the same metrics
			query := fmt.Sprintf(QUERY_FORMAT, "system.metrics", "total_disk", startTime*1000, endTime*1000)
			iter := session.Query(query).Iter()
			values := [][]float64{}
			for iter.Scan(&ts, &value, &details) {
				values = append(values, []float64{float64(ts) / 1000, float64(value) / BYTES_IN_GB})
			}
			if err := iter.Close(); err != nil {
				return ctx.String(http.StatusInternalServerError, err.Error())
			}
			sort.Slice(values, func(i, j int) bool {
				return values[i][0] < values[j][0]
			})
			query = fmt.Sprintf(QUERY_FORMAT, "system.metrics", "free_disk", startTime*1000, endTime*1000)
			iter = session.Query(query).Iter()
			freeValues := [][]float64{}
			for iter.Scan(&ts, &value, &details) {
				freeValues = append(freeValues, []float64{float64(ts) / 1000, float64(value) / BYTES_IN_GB})
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
				Values: reduceGranularity(startTime, endTime, values, GRANULARITY_NUM_INTERVALS, true),
			})
		case "PROVISIONED_DISK_SPACE_GB":
			query := fmt.Sprintf(QUERY_FORMAT, "system.metrics", "total_disk", startTime*1000, endTime*1000)
			iter := session.Query(query).Iter()
			values := [][]float64{}
			for iter.Scan(&ts, &value, &details) {
				values = append(values, []float64{float64(ts) / 1000, float64(value) / BYTES_IN_GB})
			}
			if err := iter.Close(); err != nil {
				return ctx.String(http.StatusInternalServerError, err.Error())
			}
			sort.Slice(values, func(i, j int) bool {
				return values[i][0] < values[j][0]
			})
			metricResponse.Data = append(metricResponse.Data, models.MetricData{
				Name:   metric,
				Values: reduceGranularity(startTime, endTime, values, GRANULARITY_NUM_INTERVALS, true),
			})
		case "AVERAGE_READ_LATENCY_MS":
			rawMetricValuesCount, err := getRawMetricsForAllNodes("handler_latency_yb_tserver_TabletServerService_Read_count", nodeList, hostToUuid, startTime, endTime, session, false)
			if err != nil {
				return ctx.String(http.StatusInternalServerError, err.Error())
			}

			rawMetricValuesSum, err := getRawMetricsForAllNodes("handler_latency_yb_tserver_TabletServerService_Read_sum", nodeList, hostToUuid, startTime, endTime, session, false)
			if err != nil {
				return ctx.String(http.StatusInternalServerError, err.Error())
			}

			rateMetricsCount := convertRawMetricsToRates(rawMetricValuesCount)
			rateMetricsSum := convertRawMetricsToRates(rawMetricValuesSum)

			latencyMetric := divideMetricForAllNodes(rateMetricsSum, rateMetricsCount)

			nodeMetricValues := reduceGranularityForAllNodes(startTime, endTime, latencyMetric, GRANULARITY_NUM_INTERVALS, true)

			metricValues := calculateCombinedMetric(nodeMetricValues, true)
			metricResponse.Data = append(metricResponse.Data, models.MetricData{
				Name:   metric,
				Values: metricValues,
			})
		case "AVERAGE_WRITE_LATENCY_MS":
			rawMetricValuesCount, err := getRawMetricsForAllNodes("handler_latency_yb_tserver_TabletServerService_Write_count", nodeList, hostToUuid, startTime, endTime, session, false)
			if err != nil {
				return ctx.String(http.StatusInternalServerError, err.Error())
			}

			rawMetricValuesSum, err := getRawMetricsForAllNodes("handler_latency_yb_tserver_TabletServerService_Write_sum", nodeList, hostToUuid, startTime, endTime, session, false)
			if err != nil {
				return ctx.String(http.StatusInternalServerError, err.Error())
			}

			rateMetricsCount := convertRawMetricsToRates(rawMetricValuesCount)
			rateMetricsSum := convertRawMetricsToRates(rawMetricValuesSum)

			latencyMetric := divideMetricForAllNodes(rateMetricsSum, rateMetricsCount)

			nodeMetricValues := reduceGranularityForAllNodes(startTime, endTime, latencyMetric, GRANULARITY_NUM_INTERVALS, true)

			metricValues := calculateCombinedMetric(nodeMetricValues, true)
			metricResponse.Data = append(metricResponse.Data, models.MetricData{
				Name:   metric,
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
	httpClient := &http.Client{
		Timeout: time.Second * 10,
	}
	url := fmt.Sprintf("http://%s:7000/api/v1/tablet-servers", host)
	resp, err := httpClient.Get(url)
	if err != nil {
		return ctx.String(http.StatusInternalServerError, err.Error())
	}
	defer resp.Body.Close()
	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return ctx.String(http.StatusInternalServerError, err.Error())
	}
	var result map[string]interface{}
	json.Unmarshal([]byte(body), &result)

	if val, ok := result["error"]; ok {
		return ctx.String(http.StatusInternalServerError, val.(string))
	}
	for _, obj := range result {
		for hostport, nodeDataObj := range obj.(map[string]interface{}) {
			nodeData := nodeDataObj.(map[string]interface{})
			host, _, err := net.SplitHostPort(hostport)
			// If we can split hostport, just use host as name.
			// Otherwise, use hostport as name.
			hostName := hostport
			if err == nil {
				hostName = host
			}
			totalSstFileSizeBytes := int64(nodeData["total_sst_file_size_bytes"].(float64))
			uncompressedSstFileSizeBytes := int64(nodeData["uncompressed_sst_file_size_bytes"].(float64))
			// For now, assuming that IsMaster and IsTserver are always true
			// The UI frontend doesn't use these values so this should be ok for now
			response.Data = append(response.Data, models.NodeData{
				Name:      hostName,
				IsNodeUp:  nodeData["status"].(string) == "ALIVE",
				IsMaster:  true,
				IsTserver: true,
				Metrics: models.NodeDataMetrics{
					MemoryUsedBytes:              int64(nodeData["ram_used_bytes"].(float64)),
					TotalSstFileSizeBytes:        &totalSstFileSizeBytes,
					UncompressedSstFileSizeBytes: &uncompressedSstFileSizeBytes,
					ReadOpsPerSec:                nodeData["read_ops_per_sec"].(float64),
					WriteOpsPerSec:               nodeData["write_ops_per_sec"].(float64),
				},
				CloudInfo: models.NodeDataCloudInfo{
					Region: nodeData["region"].(string),
					Zone:   nodeData["zone"].(string),
				},
			})
		}
	}
	return ctx.JSON(http.StatusOK, response)
}

// GetClusterTables - Get list of DB tables per YB API (YCQL/YSQL)
func (c *Container) GetClusterTables(ctx echo.Context) error {
	return ctx.JSON(http.StatusOK, models.HelloWorld{
		Message: "Hello World",
	})
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
		futures := []chan LiveQueriesYsqlFuture{}
		for _, nodeHost := range nodes {
			future := make(chan LiveQueriesYsqlFuture)
			futures = append(futures, future)
			go getLiveQueriesYsqlFuture(nodeHost, future)
		}
		for _, future := range futures {
			items := <-future
			if items.Error != nil {
				liveQueryResponse.Data.Ysql.ErrorCount++
				continue
			}
			for _, item := range items.Items {
				liveQueryResponse.Data.Ysql.Queries = append(liveQueryResponse.Data.Ysql.Queries, *item)
			}
		}
	}
	if api == "YCQL" {
		liveQueryResponse.Data.Ycql = models.LiveQueryResponseYcqlData{
			ErrorCount: 0,
			Queries:    []models.LiveQueryResponseYcqlQueryItem{},
		}
		// Get live queries of all nodes in parallel
		futures := []chan LiveQueriesYcqlFuture{}
		for _, nodeHost := range nodes {
			future := make(chan LiveQueriesYcqlFuture)
			futures = append(futures, future)
			go getLiveQueriesYcqlFuture(nodeHost, future)
		}
		for _, future := range futures {
			items := <-future
			if items.Error != nil {
				liveQueryResponse.Data.Ycql.ErrorCount++
				continue
			}
			for _, item := range items.Items {
				liveQueryResponse.Data.Ycql.Queries = append(liveQueryResponse.Data.Ycql.Queries, *item)
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
				// Currently, the OpenAPI spec has these set to "number" which becomes float32 in go.
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
