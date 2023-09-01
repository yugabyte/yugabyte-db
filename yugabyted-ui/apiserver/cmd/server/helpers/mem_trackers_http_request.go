package helpers

import (
    "fmt"
    "io/ioutil"
    "net/http"
    "regexp"
    "time"
)

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
    url := fmt.Sprintf("http://%s:%s/mem-trackers?raw=1", hostName, port)
    resp, err := httpClient.Get(url)
    if err != nil {
        memTrackers.Error = err
        future <- memTrackers
        return
    }
    defer resp.Body.Close()
    body, err := ioutil.ReadAll(resp.Body)
    if err != nil {
        memTrackers.Error = err
        future <- memTrackers
        return
    }
    // parse raw mem trackers response
    regex, err := regexp.Compile(`<td><span class=\"toggle collapse\">` +
        `<\/span>root<\/td><td>(.*)<\/td><td>(.*)<\/td><td>(.*)<\/td>`)
    if err != nil {
        memTrackers.Error = err
        future <- memTrackers
        return
    }
    match := regex.FindSubmatch(body)
    memTrackers.Consumption, err = h.GetBytesFromString(string(match[1]))
    if err != nil {
        memTrackers.Error = err
        future <- memTrackers
        return
    }
    memTrackers.Limit, memTrackers.Error = h.GetBytesFromString(string(match[3]))
    future <- memTrackers
}
