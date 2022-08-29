package helpers

import (
    "encoding/json"
    "fmt"
    "io/ioutil"
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

func GetVersionFuture(hostName string, future chan VersionInfoFuture) {
    versionInfo := VersionInfoFuture{
        VersionInfo: VersionInfoStruct{},
        Error: nil,
    }
    httpClient := &http.Client{
        Timeout: time.Second * 10,
    }
    url := fmt.Sprintf("http://%s:7000/api/v1/version", hostName)
    resp, err := httpClient.Get(url)
    if err != nil {
        versionInfo.Error = err
        future <- versionInfo
        return
    }
    defer resp.Body.Close()
    body, err := ioutil.ReadAll(resp.Body)
    if err != nil {
        versionInfo.Error = err
        future <- versionInfo
        return
    }
    versionInfo.Error = json.Unmarshal([]byte(body), &versionInfo.VersionInfo)
    future <- versionInfo
}
