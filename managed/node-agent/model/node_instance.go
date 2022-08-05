// Copyright (c) YugaByte, Inc.

package model

import "fmt"

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
	Provider         Provider                `json:"provider"`
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

type NodeCapabilityRequest struct {
	Nodes []NodeDetails `json:"nodes"`
}

type NodeCapabilityResponse struct {
	NodeUuid string `json:"nodeUuid"`
}

type NodeDetails struct {
	IP           string       `json:"ip"`
	Region       string       `json:"region"`
	Zone         string       `json:"zone"`
	InstanceType string       `json:"instanceType"`
	InstanceName string       `json:"instanceName"`
	NodeName     string       `json:"nodeName"`
	NodeConfigs  []NodeConfig `json:"nodeConfigurations"`
}

type NodeConfig struct {
	Type  string `json:"type"`
	Value string `json:"value"`
}

func (p Provider) ToString() string {
	return fmt.Sprintf("Provder ID: %s, Provider Name: %s", p.Uuid, p.Name)
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
