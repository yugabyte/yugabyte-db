package handlers

import (
    "apiserver/cmd/server/helpers"

    "bytes"
    "encoding/json"
    "fmt"
    "io"
    "net/http"
    "net/http/httputil"
    "time"

    "github.com/labstack/echo/v4"
)

const CALLHOME_URL = "https://diagnostics.yugabyte.com"

type CallhomeStruct struct {
    ClusterUuid string `json:"cluster_uuid"`
    NodeUuid string `json:"node_uuid"`
    ServerType string `json:"server_type"`
    Timestamp int64 `json:"timestamp"`
    Payload map[string]interface{} `json:"payload"`
}

func (c *Container) PostCallhome(ctx echo.Context) error {

    // Get cluster UUID and node UUID
    clusterConfigFuture := make(chan helpers.ClusterConfigFuture)
    go c.helper.GetClusterConfigFuture(helpers.HOST, clusterConfigFuture)
    tabletServersFuture := make(chan helpers.TabletServersFuture)
    go c.helper.GetTabletServersFuture(helpers.HOST, tabletServersFuture)

    payload, err := io.ReadAll(ctx.Request().Body)
    if err != nil {
        return ctx.String(http.StatusInternalServerError,
            fmt.Sprintf("Failed to read callhome payload: %v", err))
    }

    payloadJson := map[string]interface{}{}
    err = json.Unmarshal(payload, &payloadJson)
    if err != nil {
        return ctx.String(http.StatusInternalServerError,
            fmt.Sprintf("Failed to unmarshal callhome payload: %v", err))
    }

    var clusterUuid, nodeUuid string

    clusterConfigResponse := <-clusterConfigFuture
    if clusterConfigResponse.Error == nil {
        resultConfig := clusterConfigResponse.ClusterConfig
        clusterUuid = resultConfig.ClusterUuid
    } else {
        // If we fail to get the cluster UUID, log an error but send the callhome request
        c.logger.Warnf("Failed to get cluster UUID: %v", clusterConfigResponse.Error)
    }

    // Get response from tabletServersFuture
    tabletServersResponse := <-tabletServersFuture
    if tabletServersResponse.Error != nil {
        c.logger.Warnf("Failed to get node UUID: %v", tabletServersResponse.Error)
    } else {
        hostToUuid := c.helper.GetHostToUuidMapFromFuture(tabletServersResponse)
        nodeUuid = hostToUuid[helpers.HOST]
    }

    // Create a callhome request and attach payload field
    postBodyObject := CallhomeStruct{
        ClusterUuid: clusterUuid,
        NodeUuid: nodeUuid,
        ServerType: "yugabyted-ui",
        Timestamp: time.Now().Unix(),
        Payload: payloadJson,
    }

    postBody, err := json.Marshal(postBodyObject)
    if err != nil {
        return ctx.String(http.StatusInternalServerError,
            fmt.Sprintf("Failed to marshal callhome payload: %v", err))
    }

    request, err := http.NewRequest("POST", CALLHOME_URL, bytes.NewReader(postBody))
    if err != nil {
        return ctx.String(http.StatusInternalServerError,
            fmt.Sprintf("Failed to create callhome request: %v", err))
    }

    request.Header.Set("Content-Type", "application/json")

    debugRequest, err := httputil.DumpRequestOut(request, true)

    if err != nil {
        c.logger.Debugf("Failed to dump request: %v", err)
    } else {
        c.logger.Debugf("Attempting callhome request: %s", debugRequest)
    }

    client := &http.Client{}
    response, err := client.Do(request)

    if err != nil {
        return ctx.String(http.StatusInternalServerError,
            fmt.Sprintf("Callhome request failed: %v", err))
    }

    defer func() {
        closeErr := response.Body.Close()
        if closeErr != nil {
            c.logger.Warnf("Error closing response body: %s", closeErr)
        }
    }()

    debugResponse, err := httputil.DumpResponse(response, true)

    if err != nil {
        c.logger.Debugf("Failed to dump response: %v", err)
    } else {
        c.logger.Debugf("Got callhome response: %s", debugResponse)
    }


    body, err := io.ReadAll(response.Body)
    if err != nil {
        return ctx.String(response.StatusCode,
            fmt.Sprintf("Failed to read response body: %s", body))
    }

    return ctx.String(response.StatusCode, string(body))
}
