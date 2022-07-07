package models

// PaymentMethodResponseSpec - Payment method response spec
type PaymentMethodResponseSpec struct {

	PaymentMethodId string `json:"payment_method_id"`

	Brand string `json:"brand"`

	Last4Digits string `json:"last_4_digits"`

	Name string `json:"name"`

	ExpYear string `json:"exp_year"`

	ExpMonth string `json:"exp_month"`

	IsDefault bool `json:"is_default"`
}
