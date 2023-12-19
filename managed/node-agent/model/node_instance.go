// Copyright (c) YugaByte, Inc.

package model

import (
	"fmt"
	"time"
)

type BasicInfo struct {
	Uuid   string `json:"uuid"`
	Code   string `json:"code"`
	Name   string `json:"name"`
	Active bool   `json:"active"`
}

// Provider is the provider object.
type Provider struct {
	BasicInfo
	Cuuid               string            `json:"customerUUID"`
	AirGapInstall       bool              `json:"airGapInstall"`
	SshPort             int               `json:"sshPort"`
	OverrideKeyValidate bool              `json:"overrideKeyValidate"`
	SetUpChrony         bool              `json:"setUpChrony"`
	NtpServers          []string          `json:"ntpServers"`
	ShowSetUpChrony     bool              `json:"showSetUpChrony"`
	Config              map[string]string `json:"config"`
	Regions             []Region          `json:"regions"`
	Details             ProviderDetails   `json:"details"`
}

// ProviderDetails contains the details object within a provider.
// Only the required fields are added here.
type ProviderDetails struct {
	NodeExporterPort int  `json:"nodeExporterPort"`
	SkipProvisioning bool `json:"skipProvisioning"`
}

// yyyy-MM-dd HH:mm:ss
type Date time.Time

func (date *Date) UnmarshalJSON(b []byte) error {
	t, err := time.Parse("2006-01-02 15:04:05", string(b))
	if err != nil {
		return err
	}
	*date = Date(t)
	return nil
}

type AccessKeyInfo struct {
	SshUser                string   `json:"sshUser"`
	SshPort                int      `json:"sshPort"`
	AirGapInstall          bool     `json:"airGapInstall"`
	PasswordlessSudoAccess bool     `json:"passwordlessSudoAccess"`
	InstallNodeExporter    bool     `json:"installNodeExporter"`
	NodeExporterPort       int      `json:"nodeExporterPort"`
	NodeExporterUser       string   `json:"nodeExporterUser"`
	SetUpChrony            bool     `json:"setUpChrony"`
	NtpServers             []string `json:"ntpServers"`
	CreatetionDate         Date     `json:"creationDate"`
	SkipProvisioning       bool     `json:"skipProvisioning"`
}

type AccessKey struct {
	KeyInfo AccessKeyInfo `json:"keyInfo"`
}

type AccessKeys []AccessKey

// Implement sort.Interface.
func (keys AccessKeys) Len() int {
	return len(keys)
}

// Implement sort.Interface in descending order of
// creation time.
func (keys AccessKeys) Less(i, j int) bool {
	keyInfo1 := keys[i].KeyInfo
	keyInfo2 := keys[j].KeyInfo
	cTime1 := time.Time(keyInfo1.CreatetionDate)
	cTime2 := time.Time(keyInfo2.CreatetionDate)
	return cTime1.Before(cTime2)
}

// Implement sort.Interface.
func (keys AccessKeys) Swap(i, j int) {
	keys[i], keys[j] = keys[j], keys[i]
}

type Region struct {
	BasicInfo
	Longitude float64           `json:"longitude"`
	Latitude  float64           `json:"latitude"`
	Config    map[string]string `json:"config"`
	Zones     []Zone            `json:"zones"`
}

type Zone struct {
	BasicInfo
}

type NodeInstanceType struct {
	Active           bool                    `json:"active"`
	NumCores         float64                 `json:"numCores"`
	MemSizeGB        float64                 `json:"memSizeGB"`
	Details          NodeInstanceTypeDetails `json:"instanceTypeDetails"`
	InstanceTypeCode string                  `json:"instanceTypeCode"`
	ProviderUuid     string                  `json:"providerUuid"`
}

type NodeInstanceTypeDetails struct {
	VolumeDetailsList []VolumeDetails `json:"volumeDetailsList"`
}

type VolumeDetails struct {
	VolumeSize float64 `json:"volumeSizeGB"` //in GB
	MountPath  string  `json:"mountPath"`
}

type PreflightCheckVal struct {
	Value string `json:"value"`
}

type NodeInstances struct {
	Nodes []NodeDetails `json:"nodes"`
}

type NodeInstanceResponse struct {
	NodeUuid string `json:"nodeUuid"`
}

type NodeDetails struct {
	IP           string       `json:"ip"`
	Region       string       `json:"region"`
	Zone         string       `json:"zone"`
	InstanceType string       `json:"instanceType"`
	InstanceName string       `json:"instanceName"`
	NodeName     string       `json:"nodeName"`
	SshUser      string       `json:"sshUser"`
	NodeConfigs  []NodeConfig `json:"nodeConfigs"`
}

// PreflightCheckParam is the param for PreflightCheckHandler.
type PreflightCheckParam struct {
	SkipProvisioning     bool     `json:"skipProvisioning"`
	AirGapInstall        bool     `json:"airGapInstall"`
	InstallNodeExporter  bool     `json:"installNodeExporter"`
	YbHomeDir            string   `json:"ybHomeDir"`
	SshPort              int      `json:"sshPort"`
	MountPaths           []string `json:"mountPaths"`
	MasterHttpPort       int      `json:"masterHttpPort"`
	MasterRpcPort        int      `json:"masterRpcPort"`
	TserverHttpPort      int      `json:"tserverHttpPort"`
	TserverRpcPort       int      `json:"tserverRpcPort"`
	RedisServerHttpPort  int      `json:"redisServerHttpPort"`
	RedisServerRpcPort   int      `json:"redisServerRpcPort"`
	NodeExporterPort     int      `json:"nodeExporterPort"`
	YcqlServerHttpPort   int      `json:"ycqlServerHttpPort"`
	YcqlServerRpcPort    int      `json:"ycqlServerRpcPort"`
	YsqlServerHttpPort   int      `json:"ysqlServerHttpPort"`
	YsqlServerRpcPort    int      `json:"ysqlServerRpcPort"`
	YbControllerHttpPort int      `json:"ybControllerHttpPort"`
	YbControllerRpcPort  int      `json:"ybControllerRpcPort"`
}

type NodeConfig struct {
	Type  string `json:"type"`
	Value string `json:"value"`
}

type NodeInstanceValidationResponse struct {
	Type        string `json:"string"`
	Valid       bool   `json:"valid"`
	Required    bool   `json:"required"`
	Description string `json:"description"`
	Value       string `json:"value"`
}

// Id implements the method in DisplayInterface.
func (p Provider) Id() string {
	return p.Uuid
}

// String implements the method in DisplayInterface.
func (p Provider) String() string {
	return fmt.Sprintf("Provider ID: %s, Provider Name: %s", p.Uuid, p.Name)
}

// Id implements the method in DisplayInterface.
func (i NodeInstanceType) Id() string {
	return i.InstanceTypeCode
}

// String implements the method in DisplayInterface.
func (i NodeInstanceType) String() string {
	return fmt.Sprintf("Instance Code: %s", i.InstanceTypeCode)
}

// Id implements the method in DisplayInterface.
func (r Region) Id() string {
	return r.Code
}

// String implements the method in DisplayInterface.
func (r Region) String() string {
	return fmt.Sprintf("Region ID: %s, Region Code: %s", r.Uuid, r.Code)
}

// Id implements the method in DisplayInterface.
func (z Zone) Id() string {
	return z.Code
}

// String implements the method in DisplayInterface.
func (z Zone) String() string {
	return fmt.Sprintf("Zone ID: %s, Zone Code: %s", z.Uuid, z.Code)
}
