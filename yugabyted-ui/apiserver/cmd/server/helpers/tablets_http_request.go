package helpers

import (
    "encoding/json"
    "fmt"
    "io/ioutil"
    "net/http"
    "time"
)

type TabletOnDiskSizeStruct struct {
    TotalSize                    string `json:"total_size"`
    TotalSizeBytes               uint64 `json:"total_size_bytes"`
    ConsensusMetadataSize        string `json:"consensus_metadata_size"`
    ConsensusMetadataSizeBytes   uint64 `json:"consensus_metadata_size_bytes"`
    WalFilesSize                 string `json:"wal_files_size"`
    WalFilesSizeBytes            uint64 `json:"wal_files_size_bytes"`
    SstFilesSize                 string `json:"sst_files_size"`
    SstFilesSizeBytes            uint64 `json:"sst_files_size_bytes"`
    UncompressedSstFileSize      string `json:"uncompressed_sst_file_size"`
    UncompressedSstFileSizeBytes uint64 `json:"uncompressed_sst_file_size_bytes"`
}

type TabletInfoStruct struct {
    Namespace   string                 `json:"namespace"`
    TableName   string                 `json:"table_name"`
    TableId     string                 `json:"table_id"`
    Partition   string                 `json:"partition"`
    State       string                 `json:"string"`
    Hidden      bool                   `json:"hidden"`
    NumSstFiles uint64                 `json:"num_sst_files"`
    OnDiskSize  TabletOnDiskSizeStruct `json:"on_disk_size"`
    RaftConfig  []map[string]string    `json:"raft_config"`
    Status      string                 `json:"status"`
}

// Tablets maps tablet ID to tablet info
type TabletsFuture struct {
    Tablets map[string]TabletInfoStruct
    Error   error
}

// Gets tablets from a single tserver
func (h *HelperContainer) GetTabletsFuture(nodeHost string, future chan TabletsFuture) {
    tablets := TabletsFuture{
        Tablets: map[string]TabletInfoStruct{},
        Error: nil,
    }
    httpClient := &http.Client{
        Timeout: time.Second * 10,
    }
    url := fmt.Sprintf("http://%s:%s/api/v1/tablets", nodeHost, TserverUIPort)
    resp, err := httpClient.Get(url)
    if err != nil {
        tablets.Error = err
        future <- tablets
        return
    }
    defer resp.Body.Close()
    body, err := ioutil.ReadAll(resp.Body)
    if err != nil {
        tablets.Error = err
        future <- tablets
        return
    }
    err = json.Unmarshal([]byte(body), &tablets.Tablets)
    tablets.Error = err
    future <- tablets
}
