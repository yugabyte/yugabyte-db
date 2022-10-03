package helpers

import (
    "bytes"
    "fmt"
    "io/ioutil"
    "net/http"
    "regexp"
    "time"
)

type GFlagsFuture struct {
    GFlags map[string]string
    Error error
}

func GetGFlagsFuture(hostName string, isMaster bool, future chan GFlagsFuture) {
    port := "9000"
    if isMaster {
        port = "7000"
    }
    gFlags := GFlagsFuture {
        GFlags: map[string]string{},
        Error: nil,
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
