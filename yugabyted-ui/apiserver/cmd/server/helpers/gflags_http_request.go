package helpers

import (
    "bytes"
    "encoding/json"
    "fmt"
    "io/ioutil"
    "net/http"
    "regexp"
    "time"
)

type GFlag struct {
    Name  string `json:"name"`
    Value string `json:"value"`
    Type  string `json:"type"`
}

type GFlagsFuture struct {
    GFlags map[string]string
    Error  error
}

type GFlagsResponse struct {
    Flags []GFlag `json:"flags"`
}

type GFlagsJsonFuture struct {
    GFlags []GFlag
    Error  error
}

func (h *HelperContainer) GetGFlagsFuture(
    hostName string,
    isMaster bool,
    future chan GFlagsFuture,
) {
    port := TserverUIPort
    if isMaster {
        port = MasterUIPort
    }
    gFlags := GFlagsFuture{
        GFlags: map[string]string{},
        Error:  nil,
    }
    httpClient := &http.Client{
        Timeout: time.Second * 10,
    }
    url := fmt.Sprintf("http://%s:%s/varz?raw=1", hostName, port)
    resp, err := httpClient.Get(url)
    if err != nil {
        gFlags.Error = err
        future <- gFlags
        return
    }
    defer resp.Body.Close()
    body, err := ioutil.ReadAll(resp.Body)
    if err != nil {
        gFlags.Error = err
        future <- gFlags
        return
    }
    // parse raw gflag response
    lines := bytes.Split(body, []byte("\n"))
    regex, err := regexp.Compile(`--(.*)=(.*)`)
    if err != nil {
        gFlags.Error = err
        future <- gFlags
        return
    }
    for _, line := range lines {
        matches := regex.FindAllSubmatch(line, -1)
        for _, v := range matches {
            gFlags.GFlags[string(v[1])] = string(v[2])
        }
    }
    future <- gFlags
}

func (h *HelperContainer) GetGFlagsJsonFuture(
    hostName string,
    isMaster bool,
    future chan GFlagsJsonFuture,
) {

    port := TserverUIPort
    if isMaster {
        port = MasterUIPort
    }

    gFlags := GFlagsJsonFuture{
        GFlags: []GFlag{},
        Error:  nil,
    }
    httpClient := &http.Client{
        Timeout: time.Second * 10,
    }
    url := fmt.Sprintf("http://%s:%s/api/v1/varz", hostName, port)
    resp, err := httpClient.Get(url)
    if err != nil {
        gFlags.Error = err
        future <- gFlags
        return
    }
    defer resp.Body.Close()
    body, err := ioutil.ReadAll(resp.Body)
    if err != nil {
        gFlags.Error = err
        future <- gFlags
        return
    }
    flagsResponse := GFlagsResponse{}
    err = json.Unmarshal(body, &flagsResponse)
    gFlags.GFlags = flagsResponse.Flags
    gFlags.Error = err
    future <- gFlags
}
