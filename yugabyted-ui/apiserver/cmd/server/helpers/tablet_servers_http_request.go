package helpers

import (
        "apiserver/cmd/server/logger"
        "encoding/json"
        "errors"
        "net"
        "regexp"
)

type PathMetrics struct {
        Path           string `json:"path"`
        SpaceUsed      uint64 `json:"space_used"`
        TotalSpaceSize uint64 `json:"total_space_size"`
}

type TabletServer struct {
        TimeSinceHb                  string        `json:"time_since_hb"`
        TimeSinceHbSec               float64       `json:"time_since_hb_sec"`
        Status                       string        `json:"status"`
        UptimeSeconds                uint64        `json:"uptime_seconds"`
        RamUsed                      string        `json:"ram_used"`
        RamUsedBytes                 uint64        `json:"ram_used_bytes"`
        NumSstFiles                  uint64        `json:"num_sst_files"`
        TotalSstFileSize             string        `json:"total_sst_file_size"`
        TotalSstFileSizeBytes        uint64        `json:"total_sst_file_size_bytes"`
        UncompressedSstFileSize      string        `json:"uncompressed_sst_file_size"`
        UncompressedSstFileSizeBytes uint64        `json:"uncompressed_sst_file_size_bytes"`
        PathMetrics                  []PathMetrics `json:"path_metrics"`
        ReadOpsPerSec                float64       `json:"read_ops_per_sec"`
        WriteOpsPerSec               float64       `json:"write_ops_per_sec"`
        UserTabletsTotal             uint64        `json:"user_tablets_total"`
        UserTabletsLeaders           uint64        `json:"user_tablets_leaders"`
        SystemTabletsTotal           uint64        `json:"system_tablets_total"`
        SystemTabletsLeaders         uint64        `json:"system_tablets_leaders"`
        ActiveTablets                uint64        `json:"active_tablets"`
        Cloud                        string        `json:"cloud"`
        Region                       string        `json:"region"`
        Zone                         string        `json:"zone"`
}

type TabletServersFuture struct {
        Tablets map[string]map[string]TabletServer
        Error   error
}

func GetTabletServersFuture(log logger.Logger, nodeHost string, future chan TabletServersFuture) {
        tabletServers := TabletServersFuture{
                Tablets: map[string]map[string]TabletServer{},
                Error:   nil,
        }
        urls, err := BuildMasterURLs(log, "api/v1/tablet-servers")
        if err != nil {
                tabletServers.Error = err
                future <- tabletServers
                return
        }
        body, err := AttemptGetRequests(log, urls, true)
        if err != nil {
                tabletServers.Error = err
                future <- tabletServers
                return
        }
        var result map[string]interface{}
        err = json.Unmarshal([]byte(body), &result)
        if err != nil {
                tabletServers.Error = err
                future <- tabletServers
                return
        }
        if val, ok := result["error"]; ok {
                tabletServers.Error = errors.New(val.(string))
                future <- tabletServers
                return
        }
        err = json.Unmarshal([]byte(body), &tabletServers.Tablets)
        tabletServers.Error = err
        future <- tabletServers
}

// Helper for getting the hostnames of each node given a TabletServersFuture response
func GetNodesList(tablets TabletServersFuture) []string {
        hostNames := []string{}
        for _, obj := range tablets.Tablets {
                for hostport := range obj {
                        host, _, err := net.SplitHostPort(hostport)
                        if err == nil {
                                hostNames = append(hostNames, host)
                        }
                }
        }
        return hostNames
}

// Helper for getting a map between hostnames and uuids for tservers
// For now, we hit the /tablet-servers endpoint and parse the html
func GetHostToUuidMap(log logger.Logger, nodeHost string) (map[string]string, error) {
        hostToUuidMap := map[string]string{}
        urls, err := BuildMasterURLs(log, "tablet-servers")
        if err != nil {
                return hostToUuidMap, err
        }
        body, err := AttemptGetRequests(log, urls, false)
        if err != nil {
                return hostToUuidMap, err
        }
        // Now we parse the html to get the hostnames and uuids
        // This regex will not work if the layout of the page changes. In the future, it would be
        // better if this information can be found in a json endpoint.
        regex, err := regexp.Compile(`<\/tr>\s*<tr>\s*<td><a href=.*?>(.*?)<\/a><\/br>\s*(.*?)<\/td>`)
        if err != nil {
                return hostToUuidMap, err
        }
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
