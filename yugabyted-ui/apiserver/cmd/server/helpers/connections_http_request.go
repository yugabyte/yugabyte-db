package helpers

import (
    "encoding/json"
    "fmt"
    "io/ioutil"
    "net/http"
    "time"
)

type ConnectionPool struct {
    DatabaseName                    string `json:"database_name"`
    UserName                        string `json:"user_name"`
    ActiveLogicalConnections        int64  `json:"active_logical_connections"`
    QueuedLogicalConnections        int64  `json:"queued_logical_connections"`
    WaitingLogicalConnections       int64  `json:"waiting_logical_connections"`
    ActivePhysicalConnections       int64  `json:"active_physical_connections"`
    IdlePhysicalConnections         int64  `json:"idle_physical_connections"`
    AvgWaitTimeNs                   int64  `json:"avg_wait_time_ns"`
    Qps                             int64  `json:"qps"`
    Tps                             int64  `json:"tps"`
}

type ConnectionsFuture struct {
    Pools []ConnectionPool `json:"pools"`
    Error error
}

func (h *HelperContainer) GetConnectionsFuture(nodeHost string, future chan ConnectionsFuture) {
    connections := ConnectionsFuture{
        Pools: []ConnectionPool{},
        Error: nil,
    }
    httpClient := &http.Client{
        Timeout: time.Second * 10,
    }
    url := fmt.Sprintf("http://%s:13000/connections", nodeHost)
    resp, err := httpClient.Get(url)
    if err != nil {
        connections.Error = err
        future <- connections
        h.logger.Errorf("failed to get connections from node %s: %s", nodeHost, err.Error())
        return
    }
    defer resp.Body.Close()
    body, err := ioutil.ReadAll(resp.Body)
    if err != nil {
        connections.Error = err
        future <- connections
        h.logger.Errorf("failed to read connections json response from node %s: %s",
            nodeHost, err.Error())
        return
    }
    err = json.Unmarshal([]byte(body), &connections)
    connections.Error = err
    future <- connections
}
