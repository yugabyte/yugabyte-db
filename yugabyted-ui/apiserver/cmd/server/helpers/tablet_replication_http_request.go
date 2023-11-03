package helpers

import (
    "encoding/json"
    "fmt"
    "io/ioutil"
    "net/http"
    "time"
)

type TabletReplicationInfo struct {
    TableUuid string `json:"table_uuid"`
    TabletUuid string `json:"tablet_uuid"`
}

type TabletReplicationFuture struct {
    LeaderlessTablets []TabletReplicationInfo
    Error error
}

func (h *HelperContainer) GetTabletReplicationFuture(
    nodeHost string,
    future chan TabletReplicationFuture,
) {
    leaderlessTablets := TabletReplicationFuture{
        LeaderlessTablets: []TabletReplicationInfo{},
        Error: nil,
    }
    httpClient := &http.Client{
        Timeout: time.Second * 10,
    }
    url := fmt.Sprintf("http://%s:%s/api/v1/tablet-replication", nodeHost, MasterUIPort)
    resp, err := httpClient.Get(url)
    if err != nil {
        leaderlessTablets.Error = err
        future <- leaderlessTablets
        return
    }
    defer resp.Body.Close()
    body, err := ioutil.ReadAll(resp.Body)
    if err != nil {
        leaderlessTablets.Error = err
        future <- leaderlessTablets
        return
    }
    err = json.Unmarshal([]byte(body), &leaderlessTablets.LeaderlessTablets)
    leaderlessTablets.Error = err
    future <- leaderlessTablets
}
