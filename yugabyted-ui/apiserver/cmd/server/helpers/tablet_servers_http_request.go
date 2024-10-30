package helpers

import (
        "encoding/json"
        "errors"
        "net"
        "net/url"
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
        PermanentUuid                string        `json:"permanent_uuid"`
}

type TabletServersFuture struct {
        Tablets map[string]map[string]TabletServer
        Error   error
}

func (h *HelperContainer) GetTabletServersFuture(nodeHost string, future chan TabletServersFuture) {
        tabletServers := TabletServersFuture{
                Tablets: map[string]map[string]TabletServer{},
                Error:   nil,
        }
        body, err := h.BuildMasterURLsAndAttemptGetRequests(
            "api/v1/tablet-servers", // path
            url.Values{}, // params
            true, // expectJson
        )
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
        // Update cache if successful. Note that this will happen in the background asynchronously.
        if err == nil {
            tserverAddresses := h.GetNodesList(tabletServers)
            for index, host := range tserverAddresses {
                if host == HOST {
                    // Swap helpers.HOST to front of slice
                    tserverAddresses[0], tserverAddresses[index] =
                        tserverAddresses[index], tserverAddresses[0]
                    break
                }
            }
            TserverAddressCache.Update(tserverAddresses)
            h.logger.Debugf("updated cached tserver addresses %v", tserverAddresses)
        }
}

// Helper for getting the hostnames of each node given a TabletServersFuture response.
// Assumes TabletServersFuture.Error is nil.
func (h *HelperContainer) GetNodesList(tablets TabletServersFuture) []string {
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

// Helper for getting a map between hostnames and uuids for tservers given a
// TabletServersFuture response
func (h *HelperContainer) GetHostToUuidMap(nodeHost string) (map[string]string, error) {
    tabletServersFuture := make(chan TabletServersFuture)
    go h.GetTabletServersFuture(nodeHost, tabletServersFuture)
    tabletServersResponse := <-tabletServersFuture
    if tabletServersResponse.Error != nil {
        return map[string]string{}, tabletServersResponse.Error
    }
    hostToUuid := h.GetHostToUuidMapFromFuture(tabletServersResponse)
    return hostToUuid, nil
}

// Helper for getting a map between hostnames and uuids for tservers given a
// TabletServersFuture response. Assumes TabletServersFuture.Error is nil.
func (h *HelperContainer) GetHostToUuidMapFromFuture(
    tserversFuture TabletServersFuture,
) map[string]string {
    hostToUuid := map[string]string{}
    for _, obj := range tserversFuture.Tablets {
        for hostport, tserverData := range obj {
            host, _, err := net.SplitHostPort(hostport)
            if err == nil {
                hostToUuid[host] = tserverData.PermanentUuid
            } else {
                h.logger.Warnf("failed to split hostport %s, couldn't get uuid of node", hostport)
            }
        }
    }
    h.logger.Debugf("host to uuid map: %v", hostToUuid)
    return hostToUuid
}
