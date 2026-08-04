package helpers

import (
    "encoding/json"
    "strings"
    "fmt"
)

// PITRConfig represents the entire JSON output from the yb-admin command.
type PITRConfig struct {
    Schedules []Schedule `json:"schedules"`
}

// Schedule represents each individual schedule in the output.
type Schedule struct {
    Options   Options   `json:"options"`
    Snapshots []Snapshot `json:"snapshots"`
}

// Options represents the options for each schedule.
type Options struct {
    Filter    string `json:"filter"`
    Interval  string `json:"interval"`
    Retention string `json:"retention"`
    DeleteTime string `json:"delete_time,omitempty"`
}

// Snapshot represents each snapshot within a schedule.
type Snapshot struct {
    SnapshotTime string `json:"snapshot_time"`
}

type PITRConfigFuture struct {
    Config PITRConfig
    Error  error
}

func (h *HelperContainer) GetPITRConfigFuture(future chan PITRConfigFuture) {
    pitrConfigResult := PITRConfigFuture{
        Config: PITRConfig{},
        Error:  nil,
    }
    h.logger.Infof("Starting to fetch PITR configuration")
    masterAddresses := MasterAddressCache.Get()
    masterAddressesString := strings.Join(masterAddresses, ",")
    ybAdminResult, ybAdminError := h.ListSnapshotSchedules(masterAddressesString)
    if ybAdminError != nil {
        // Fetch updated master addresses and retry
        updatedMasterAddressesFuture := make(chan MasterAddressesFuture)
        go h.GetMasterAddressesFuture(updatedMasterAddressesFuture)
        updatedMasterAddressesResult := <-updatedMasterAddressesFuture
        if updatedMasterAddressesResult.Error != nil {
            // If fetching updated addresses also fails, return an error
            pitrConfigResult.Error = fmt.Errorf("failed to fetch updated master addresses: %v",
                                                             updatedMasterAddressesResult.Error)
            future <- pitrConfigResult
            return
        }
        // Try with updated addresses
        updatedMasterAddressesString := strings.Join(updatedMasterAddressesResult.HostList, ",")
        ybAdminResult, ybAdminError = h.ListSnapshotSchedules(updatedMasterAddressesString)
        if ybAdminError != nil {
            // If the second attempt also fails, return an error
            pitrConfigResult.Error = fmt.Errorf("failed to list snapshot schedules: %v",
                                                                            ybAdminError)
            future <- pitrConfigResult
            return
        }
    }
    err := json.Unmarshal([]byte(ybAdminResult), &pitrConfigResult.Config)
    if err != nil {
    pitrConfigResult.Error = fmt.Errorf("failed to unmarshal snapshot schedules: %v", err)
    }
    future <- pitrConfigResult
}
