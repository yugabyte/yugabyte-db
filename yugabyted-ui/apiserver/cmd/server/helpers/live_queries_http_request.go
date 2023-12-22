package helpers
import (
    "apiserver/cmd/server/models"
    "encoding/json"
    "fmt"
    "io/ioutil"
    "net"
    "net/http"
    "strings"
    "time"
)
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
    State             string                             `json:"state"`
}

type LiveQueryHttpYsqlResponse struct {
    Connections []*LiveQueryHttpYsqlResponseConnection `json:"connections"`
    Error       string                                 `json:"error"`
}

type LiveQueryHttpYcqlResponse struct {
    InboundConnections []*LiveQueryHttpYcqlResponseInboundConnection `json:"inbound_connections"`
    Error              string                                        `json:"error"`
}

type LiveQueriesYsqlFuture struct {
    Items []*models.LiveQueryResponseYsqlQueryItem
    Error error
}

type LiveQueriesYcqlFuture struct {
    Items []*models.LiveQueryResponseYcqlQueryItem
    Error error
}

func (h *HelperContainer) GetLiveQueriesYsqlFuture(
    nodeHost string,
    future chan LiveQueriesYsqlFuture,
) {
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
            connection.BackendType == "client backend" && connection.SessionStatus == "idle" {
            // uuid is just a random number, it is used in the frontend as a table row key.
            uuid, err := h.Random128BitString()
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

func (h *HelperContainer) GetLiveQueriesYcqlFuture(
    nodeHost string,
    future chan LiveQueriesYcqlFuture,
) {
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
                    uuid, err := h.Random128BitString()
                    if err != nil {
                        liveQueries.Error = err
                        future <- liveQueries
                        return
                    }
                    keyspace := inboundConnection.ConnectionDetails.CqlConnectionDetails.Keyspace
                    // for now NodeName is the node host
                    connectionToAdd := models.LiveQueryResponseYcqlQueryItem{
                        Id:            uuid,
                        NodeName:      nodeHost,
                        Keyspace:      keyspace,
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

type ActiveYsqlConnectionsFuture struct {
    YsqlConnections int64
    Error           error
}

type ActiveYcqlConnectionsFuture struct {
    YcqlConnections int64
    Error           error
}

func (h *HelperContainer) GetActiveYsqlConnectionsFuture(
    nodeHost string,
    future chan ActiveYsqlConnectionsFuture,
) {
    activeConnections := ActiveYsqlConnectionsFuture{
        YsqlConnections: 0,
        Error: nil,
    }
    httpClient := &http.Client{
        Timeout: time.Second * 10,
    }
    url := fmt.Sprintf("http://%s:13000/rpcz", nodeHost)
    resp, err := httpClient.Get(url)
    if err != nil {
        activeConnections.Error = err
        future <- activeConnections
        return
    }
    defer resp.Body.Close()
    body, err := ioutil.ReadAll(resp.Body)
    if err != nil {
        activeConnections.Error = err
        future <- activeConnections
        return
    }
    var ysqlResponse LiveQueryHttpYsqlResponse
    json.Unmarshal([]byte(body), &ysqlResponse)
    for _, connection := range ysqlResponse.Connections {
        if connection.SessionStatus != "" && connection.SessionStatus == "active" {
            activeConnections.YsqlConnections++
        }
    }
    future <- activeConnections
}

func (h *HelperContainer) GetActiveYcqlConnectionsFuture(
    nodeHost string,
    future chan ActiveYcqlConnectionsFuture,
) {
    activeConnections := ActiveYcqlConnectionsFuture{
        YcqlConnections: 0,
        Error: nil,
    }
    httpClient := &http.Client{
        Timeout: time.Second * 10,
    }
    url := fmt.Sprintf("http://%s:12000/rpcz", nodeHost)
    resp, err := httpClient.Get(url)
    if err != nil {
        activeConnections.Error = err
        future <- activeConnections
        return
    }
    defer resp.Body.Close()
    body, err := ioutil.ReadAll(resp.Body)
    if err != nil {
        activeConnections.Error = err
        future <- activeConnections
        return
    }
    var ycqlResponse LiveQueryHttpYcqlResponse
    json.Unmarshal([]byte(body), &ycqlResponse)
    for _, inboundConnection := range ycqlResponse.InboundConnections {
        if inboundConnection.State == "OPEN" {
            activeConnections.YcqlConnections++
        }
    }
    future <- activeConnections
}
