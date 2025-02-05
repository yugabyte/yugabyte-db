package helpers

import (
    "encoding/json"
    "fmt"
    "io/ioutil"
    "net"
    "net/http"
    "time"
)

type VersionInfoStruct struct {
    GitHash string `json:"git_hash"`
    BuildHostname string `json:"build_hostname"`
    BuildTimestamp string `json:"build_timestamp"`
    BuildUsername string `json:"build_username"`
    BuildCleanRepo bool `json:"build_clean_repo"`
    BuildId string `json:"build_id"`
    BuildType string `json:"build_type"`
    VersionNumber string `json:"version_number"`
    BuildNumber string `json:"build_number"`
}

type VersionInfoFuture struct {
    VersionInfo VersionInfoStruct
    Error error
}

func (h *HelperContainer) GetVersionFuture(
    hostName string,
    isMaster bool,
    future chan VersionInfoFuture,
) {
    versionInfo := VersionInfoFuture{
        VersionInfo: VersionInfoStruct{},
        Error: nil,
    }
    httpClient := &http.Client{
        Timeout: time.Second * 10,
    }
    port := TserverUIPort
    if isMaster {
        port = MasterUIPort
    }
    url := fmt.Sprintf("http://%s/api/v1/version", net.JoinHostPort(hostName, port))
    resp, err := httpClient.Get(url)
    if err != nil {
        versionInfo.Error = err
        h.logger.Warnf("failed to get version from url %s: %s", url, err.Error())
        future <- versionInfo
        return
    }
    defer resp.Body.Close()
    body, err := ioutil.ReadAll(resp.Body)
    if err != nil {
        versionInfo.Error = err
        h.logger.Warnf("failed to read version from url %s: %s", url, err.Error())
        future <- versionInfo
        return
    }
    versionInfo.Error = json.Unmarshal([]byte(body), &versionInfo.VersionInfo)
    future <- versionInfo
}
