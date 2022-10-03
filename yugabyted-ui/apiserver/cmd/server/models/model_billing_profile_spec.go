package models

// BillingProfileSpec - Billing profile spec
type BillingProfileSpec struct {

	ContactName string `json:"contact_name"`

	ContactEmailAddress string `json:"contact_email_address"`

	ContactPhone *string `json:"contact_phone"`

	CompanyName *string `json:"company_name"`

	InvoiceAddress AddressSpec `json:"invoice_address"`

	BillingAddress AddressSpec `json:"billing_address"`

	DunsNumber string `json:"duns_number"`

	TaxRegistrationNumber string `json:"tax_registration_number"`

	BillingType *BillingTypeSpec `json:"billing_type"`

	PaymentMethodId *string `json:"payment_method_id"`

	PaymentMethodType PaymentMethodEnum `json:"payment_method_type"`
}
