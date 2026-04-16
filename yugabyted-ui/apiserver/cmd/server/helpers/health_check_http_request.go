package helpers

import (
    "encoding/json"
    "errors"
    "net/url"
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

func (h *HelperContainer) GetHealthCheckFuture(nodeHost string, future chan HealthCheckFuture) {
    healthCheck := HealthCheckFuture{
        HealthCheck: HealthCheckStruct{},
        Error: nil,
    }
    body, err := h.BuildMasterURLsAndAttemptGetRequests(
        "api/v1/health-check", // path
        url.Values{}, // params
        true, // expectJson
    )
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
