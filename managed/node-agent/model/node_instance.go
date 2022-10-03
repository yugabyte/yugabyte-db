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

type Provider struct {
	BasicInfo
	Cuuid               string            `json:"customerUUID"`
	AirGapInstall       bool              `json:"airGapInstall"`
	SshPort             int               `json:"sshPort"`
	CustomHostCidrs     []string          `json:"customHostCidrs"`
	OverrideKeyValidate bool              `json:"overrideKeyValidate"`
	SetUpChrony         bool              `json:"setUpChrony"`
	NtpServers          []string          `json:"ntpServers"`
	ShowSetUpChrony     bool              `json:"showSetUpChrony"`
	Config              map[string]string `json:"config"`
	Regions             []Region          `json:"regions"`
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
	Error string `json:"error"`
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

func (p Provider) ToString() string {
	return fmt.Sprintf("Provider ID: %s, Provider Name: %s", p.Uuid, p.Name)
}

func (i NodeInstanceType) ToString() string {
	return fmt.Sprintf("Instance Code: %s", i.InstanceTypeCode)
}

func (r Region) ToString() string {
	return fmt.Sprintf("Region ID: %s, Region Code: %s", r.Uuid, r.Code)
}

func (z Zone) ToString() string {
	return fmt.Sprintf("Zone ID: %s, Zone Name: %s", z.Uuid, z.Name)
}
