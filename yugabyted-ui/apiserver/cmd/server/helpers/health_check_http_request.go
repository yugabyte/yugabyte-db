package helpers

import (
    "encoding/json"
    "errors"
    "fmt"
    "io/ioutil"
    "net/http"
    "time"
)

type HealthCheckStruct struct {
    DeadNodes []string `json:"dead_nodes"`
    MostRecentUptime int64 `json:"most_recent_uptime"`
    UnderReplicatedTablets []string `json:"under_replicated_tablets"`
}

type HealthCheckFuture struct {
    HealthCheck HealthCheckStruct
    Error error
}

func GetHealthCheckFuture(nodeHost string, future chan HealthCheckFuture) {
    healthCheck := HealthCheckFuture{
        HealthCheck: HealthCheckStruct{},
        Error: nil,
    }
    httpClient := &http.Client{
        Timeout: time.Second * 10,
    }
    url := fmt.Sprintf("http://%s:7000/api/v1/health-check", nodeHost)
    resp, err := httpClient.Get(url)
    if err != nil {
        healthCheck.Error = err
        future <- healthCheck
        return
    }
    defer resp.Body.Close()
    body, err := ioutil.ReadAll(resp.Body)
    if err != nil {
        healthCheck.Error = err
        future <- healthCheck
        return
    }
    var result map[string]interface{}
    err = json.Unmarshal([]byte(body), &result)
    if err != nil {
        healthCheck.Error = err
        future <- healthCheck
        return
    }
    if val, ok := result["error"]; ok {
        healthCheck.Error = errors.New(val.(string))
        future <- healthCheck
        return
    }
    err = json.Unmarshal([]byte(body), &healthCheck.HealthCheck)
    healthCheck.Error = err
    future <- healthCheck
}
