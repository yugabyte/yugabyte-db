package helpers

import (
    "encoding/json"
    "fmt"
    "io/ioutil"
    "net"
    "net/http"
    "net/url"
    "time"
)

type InstanceIdStruct struct {
    PermanentUuid string `json:"permanent_uuid"`
    InstanceSeqno int64  `json:"instance_seqno"`
    StartTimeUs   int64  `json:"start_time_us"`
}

type HostPortAddress struct {
    Host string `json:"host"`
    Port uint32 `json:"port"`
}

type CloudInfoStruct struct {
    PlacementCloud  string `json:"placement_cloud"`
    PlacementRegion string `json:"placement_region"`
    PlacementZone   string `json:"placement_zone"`
}

type RegistrationStruct struct {
    PrivateRpcAddresses []HostPortAddress `json:"private_rpc_addresses"`
    HttpAddresses []HostPortAddress `json:"http_addresses"`
    CloudInfo CloudInfoStruct `json:"cloud_info"`
    PlacementUuid string `json:"placement_uuid"`
    BroadcastAddresses []HostPortAddress `json:"broadcast_addresses"`
}

type ErrorStruct struct {
    Code string `json:"code"`
    Message string `json:"message"`
    PosixCode int32 `json:"posix_code"`
    SourceFile string `json:"source_file"`
    SourceLine int32 `json:"source_line"`
    Errors string `json:"errors"`
}

type Master struct {
    InstanceId InstanceIdStruct `json:"instance_id"`
    Registration RegistrationStruct `json:"registration"`
    Role string `json:"role"`
    Error *ErrorStruct `json:"error"`
}

type MastersFuture struct {
    Masters []Master `json:"masters"`
    Error error `json:"error"`
}

func (h *HelperContainer) GetMastersFuture(nodeHost string, future chan MastersFuture) {
    masters := MastersFuture{
        Masters: []Master{},
        Error: nil,
    }
    // Note: we don't use the BuildMasterURLs helper here, because it might call this function
    // and cause infinite recursion
    mastersListFuture := make(chan MastersListFuture)
    go h.GetMastersFromTserverFuture(HOST, mastersListFuture)
    mastersListResponse := <-mastersListFuture
    masterAddresses := []string{}
    if mastersListResponse.Error != nil {
        h.logger.Warnf("failed to get masters list from tserver at %s:",
            HOST, mastersListResponse.Error.Error())
        // In this case, assume current node is a master
        masterAddresses = append(masterAddresses, HOST)
    } else {
        for _, master := range mastersListResponse.Masters {
            host, _, err := net.SplitHostPort(master.HostPort)
            if err != nil {
                h.logger.Warnf("failed to split host and port of %s", master.HostPort)
                continue
            }
            if host == HOST {
                masterAddresses = append([]string{host}, masterAddresses...)
            } else {
                masterAddresses = append(masterAddresses, host)
            }
        }
    }
    urlList := []string{}
    for _, host := range masterAddresses {
        url, err := url.JoinPath(fmt.Sprintf("http://%s:%s", host, MasterUIPort), "api/v1/masters")
        if err != nil {
            h.logger.Warnf("failed to construct url for %s:%s with path %s",
                host, MasterUIPort, "api/v1/masters")
            continue
        }
        urlList = append(urlList, url)
    }
    body, err := h.AttemptGetRequests(urlList, true)
    if err != nil {
        masters.Error = err
        future <- masters
        return
    }
    err = json.Unmarshal([]byte(body), &masters)
    if masters.Error != nil {
        future <- masters
        return
    }
    masters.Error = err
    future <- masters
}

type MasterAddressAndType struct {
    HostPort string `json:"master_server"`
    IsLeader bool   `json:"is_leader"`
}

type MastersListFuture struct {
    Masters []MasterAddressAndType `json:"master_server_and_type"`
    Error   error
}
func (h *HelperContainer) GetMastersFromTserverFuture(
    nodeHost string,
    future chan MastersListFuture,
) {
    masters := MastersListFuture{
        Masters: []MasterAddressAndType{},
        Error:   nil,
    }
    httpClient := &http.Client{
        Timeout: time.Second * 10,
    }
    url := fmt.Sprintf("http://%s:%s/api/v1/masters", nodeHost, TserverUIPort)
    resp, err := httpClient.Get(url)
    if err != nil {
        masters.Error = err
        future <- masters
        return
    }
    defer resp.Body.Close()
    body, err := ioutil.ReadAll(resp.Body)
    if err != nil {
        masters.Error = err
        future <- masters
        return
    }
    err = json.Unmarshal([]byte(body), &masters)
    if masters.Error != nil {
        future <- masters
        return
    }
    masters.Error = err
    future <- masters
}
