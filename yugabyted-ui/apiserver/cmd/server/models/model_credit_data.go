package models

// CreditData - Credit identifier with credit spec
type CreditData struct {

	Spec CreditSpec `json:"spec"`

	Info CreditInfo `json:"info"`
}
