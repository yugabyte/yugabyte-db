package models

// CreditInfo - Credit Information
type CreditInfo struct {

	CreditId string `json:"credit_id"`

	CreditsRedeemed int32 `json:"credits_redeemed"`

	Metadata EntityMetadata `json:"metadata"`
}
