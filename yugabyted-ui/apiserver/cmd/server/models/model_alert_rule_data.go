package models

// AlertRuleData - Alert rule data
type AlertRuleData struct {

	Spec AlertRuleSpec `json:"spec"`

	Info AlertRuleInfo `json:"info"`
}
