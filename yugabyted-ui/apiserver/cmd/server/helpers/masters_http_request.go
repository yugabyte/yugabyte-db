package helpers

import (
    "encoding/json"
    "fmt"
    "io/ioutil"
    "net/http"
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

type Master struct {
    InstanceId InstanceIdStruct `json:"instance_id"`
    Registration RegistrationStruct `json:"registration"`
    Role string `json:"role"`
}

type MastersFuture struct {
    Masters []Master `json:"masters"`
    Error error `json:"error"`
}

func GetMastersFuture(nodeHost string, future chan MastersFuture) {
    masters := MastersFuture{
        Masters: []Master{},
        Error: nil,
    }
    httpClient := &http.Client{
        Timeout: time.Second * 10,
    }
    url := fmt.Sprintf("http://%s:7000/api/v1/masters", nodeHost)
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
