package models

// AddressSpec - Address spec
type AddressSpec struct {

	Country string `json:"country"`

	State string `json:"state"`

	AddressLine1 string `json:"address_line1"`

	AddressLine2 string `json:"address_line2"`

	City string `json:"city"`

	PostalCode string `json:"postal_code"`
}
