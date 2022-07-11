package helpers

import (
    "encoding/json"
    "fmt"
    "io/ioutil"
    "net/http"
    "time"
)

type PlacementBlock struct {
    CloudInfo      CloudInfoStruct `json:"cloud_info"`
    MinNumReplicas int             `json:"min_num_replicas"`
}

type LiveReplicasStruct struct {
    NumReplicas     int              `json:"num_replicas"`
    PlacementBlocks []PlacementBlock `json:"placement_blocks"`
}

type ReplicationInfoStruct struct {
    LiveReplicas LiveReplicasStruct `json:"live_replicas"`
}

type EncryptionInfoStruct struct {
    EncryptionEnabled          bool   `json:"encryption_enabled"`
    UniverseKeyRegistryEncoded string `json:"universe_key_registry_encoded"`
    KeyPath                    string `json:"key_path"`
    LatestVersionId            string `json:"latest_version_id"`
    KeyInMemory                bool   `json:"key_in_memory"`
}
type ClusterConfigStruct struct {
    Version         int                   `json:"version"`
    ReplicationInfo ReplicationInfoStruct `json:"replication_info"`
    ClusterUuid     string                `json:"cluster_uuid"`
    EncryptionInfo  EncryptionInfoStruct  `json:"encryption_info"`
}

type ClusterConfigFuture struct {
    ClusterConfig ClusterConfigStruct
    Error error
}

func GetClusterConfigFuture(nodeHost string, future chan ClusterConfigFuture) {
    clusterConfig := ClusterConfigFuture{
        ClusterConfig: ClusterConfigStruct{},
        Error: nil,
    }
    httpClient := &http.Client{
        Timeout: time.Second * 10,
    }
    url := fmt.Sprintf("http://%s:7000/api/v1/cluster-config", nodeHost)
    resp, err := httpClient.Get(url)
    if err != nil {
        clusterConfig.Error = err
        future <- clusterConfig
        return
    }
    defer resp.Body.Close()
    body, err := ioutil.ReadAll(resp.Body)
    if err != nil {
        clusterConfig.Error = err
        future <- clusterConfig
        return
    }
    err = json.Unmarshal([]byte(body), &clusterConfig.ClusterConfig)
    clusterConfig.Error = err
    future <- clusterConfig
}
