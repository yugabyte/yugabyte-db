package models

// UpdatePaymentMethodSpec - Update payment method of a billing account
type UpdatePaymentMethodSpec struct {

	PaymentMethodType PaymentMethodEnum `json:"payment_method_type"`
}
