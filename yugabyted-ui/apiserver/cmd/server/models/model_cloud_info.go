package models

// CloudInfo - Cloud deployment information
type CloudInfo struct {

    Code CloudEnum `json:"code"`

    Region string `json:"region"`
}
