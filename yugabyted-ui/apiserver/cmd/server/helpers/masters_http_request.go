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

func (h *HelperContainer) GetMastersFuture(future chan MastersFuture) {
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
        h.logger.Warnf("failed to get masters list from tserver at %s: %s",
            HOST, mastersListResponse.Error.Error())
        // In this case, assume current node is a master
        masterAddresses = append(masterAddresses, HOST)
    } else {
        masterAddresses = h.ExtractMasterAddressesList(mastersListResponse)
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
    body, _, err := h.AttemptGetRequests(urlList, true)
    if err != nil {
        masters.Error = err
        future <- masters
        return
    }
    err = json.Unmarshal([]byte(body), &masters)
    if err != nil {
        h.logger.Debugf("unable to parse json: %s", body)
        future <- masters
        return
    }
    masters.Error = err
    future <- masters
    // Update cache if successful
    if err == nil {
        masterAddresses := h.ExtractMasterAddresses(masters)
        MasterAddressCache.Update(masterAddresses)
        h.logger.Debugf("updated cached master addresses %v", masterAddresses)
    }
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
    // Update cache if successful
    if err == nil {
        masterAddresses := h.ExtractMasterAddressesList(masters)
        MasterAddressCache.Update(masterAddresses)
        h.logger.Debugf("updated cached master addresses %v", masterAddresses)
    }
}

// Get master addresses from MastersFuture, from master server response
func (h *HelperContainer) ExtractMasterAddresses(masters MastersFuture) []string {
    addresses := []string{}
    count := 0
    for _, master := range masters.Masters {
        if len(master.Registration.PrivateRpcAddresses) > 0 {
            host := master.Registration.PrivateRpcAddresses[0].Host
            addresses = append(addresses, host)
            if host == HOST {
                addresses[0], addresses[count] = addresses[count], addresses[0]
            }
            count += 1
        } else {
            h.logger.Warnf("couldn't find private rpc address of a master")
        }
    }
    return addresses
}

// Get master addresses from MastersListFuture, from tserver response
func (h *HelperContainer) ExtractMasterAddressesList(masters MastersListFuture) []string {
    addresses := []string{}
    count := 0
    for _, master := range masters.Masters {
        host, _, err := net.SplitHostPort(master.HostPort)
        if err != nil {
            h.logger.Warnf("failed to split host and port of %s", master.HostPort)
            continue
        }
        addresses = append(addresses, host)
        if host == HOST {
            addresses[0], addresses[count] = addresses[count], addresses[0]
        }
        count += 1
    }
    return addresses
}
