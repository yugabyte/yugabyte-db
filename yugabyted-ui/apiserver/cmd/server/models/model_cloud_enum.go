package models
// CloudEnum : Which cloud the cluster is deployed in
type CloudEnum string

// List of CloudEnum
const (
    CLOUDENUM_AWS CloudEnum = "AWS"
    CLOUDENUM_GCP CloudEnum = "GCP"
    CLOUDENUM_MANUAL CloudEnum = "MANUAL"
)
