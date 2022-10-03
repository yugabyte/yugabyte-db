package models

// PaymentMethodListResponseSpec - Payment method list response spec
type PaymentMethodListResponseSpec struct {

	CardList []PaymentMethodResponseSpec `json:"card_list"`
}
