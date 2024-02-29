package helpers

import (
    "encoding/json"
    "fmt"
    "io/ioutil"
    "net/http"
    "time"
)

type MemTrackersStruct struct {
    Id                      string              `json:"id"`
    LimitBytes              int64               `json:"limit_bytes"`
    CurrentConsumptionBytes int64               `json:"current_consumption_bytes"`
    PeakConsumptionBytes    int64               `json:"peak_consumption_bytes"`
    Children                []MemTrackersStruct `json:"children"`
}

type MemTrackersFuture struct {
    Consumption int64
    Limit       int64
    Error       error
}

func (h *HelperContainer) GetMemTrackersFuture(
    hostName string,
    isMaster bool,
    future chan MemTrackersFuture,
) {
    port := TserverUIPort
    if isMaster {
        port = MasterUIPort
    }
    memTrackers := MemTrackersFuture{
        Consumption: 0,
        Limit:       0,
        Error:       nil,
    }
    httpClient := &http.Client{
        Timeout: time.Second * 10,
    }
    url := fmt.Sprintf("http://%s:%s/api/v1/mem-trackers", hostName, port)
    resp, err := httpClient.Get(url)
    if err != nil {
        memTrackers.Error = err
        h.logger.Warnf("failed to get mem trackers from url %s: %s", url, err.Error())
        future <- memTrackers
        return
    }
    defer resp.Body.Close()
    body, err := ioutil.ReadAll(resp.Body)
    if err != nil {
        memTrackers.Error = err
        h.logger.Warnf("failed to read mem trackers from url %s: %s", url, err.Error())
        future <- memTrackers
        return
    }
    memTrackersResponse := MemTrackersStruct{}
    err = json.Unmarshal([]byte(body), &memTrackersResponse)
    if err != nil {
        memTrackers.Error = err
        h.logger.Warnf("failed to unmarshal the response from url %s: %s", url, err.Error())
        future <- memTrackers
        return
    }
    memTrackers.Consumption = memTrackersResponse.CurrentConsumptionBytes
    memTrackers.Limit = memTrackersResponse.LimitBytes
    future <- memTrackers
}
