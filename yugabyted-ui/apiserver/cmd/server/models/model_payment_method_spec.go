package models

// PaymentMethodSpec - Payment method spec
type PaymentMethodSpec struct {

	PaymentMethodId string `json:"payment_method_id"`

	IsDefault bool `json:"is_default"`

	PaymentMethodType PaymentMethodEnum `json:"payment_method_type"`
}
