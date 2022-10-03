package models

// VpcCloudInfo - Cloud deployment information for a VPC
type VpcCloudInfo struct {

	Code CloudEnum `json:"code"`

	Region string `json:"region"`
}
