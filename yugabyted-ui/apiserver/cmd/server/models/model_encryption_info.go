package models

// EncryptionInfo - Cluster encryption info
type EncryptionInfo struct {

    EncryptionAtRest bool `json:"encryption_at_rest"`

    EncryptionInTransit bool `json:"encryption_in_transit"`
}
