package models
// PaymentMethodEnum : Payment method enum (CARD/OTHER/EMPLOYEE)
type PaymentMethodEnum string

// List of PaymentMethodEnum
const (
	PAYMENTMETHODENUM_CREDIT_CARD PaymentMethodEnum = "CREDIT_CARD"
	PAYMENTMETHODENUM_OTHER PaymentMethodEnum = "OTHER"
	PAYMENTMETHODENUM_EMPLOYEE PaymentMethodEnum = "EMPLOYEE"
)
