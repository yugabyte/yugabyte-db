package models

// AlertRuleInfo - Alert rule info
type AlertRuleInfo struct {

	Id string `json:"id"`

	Name string `json:"name"`

	Source AlertRuleSourceEnum `json:"source"`

	Severity AlertRuleSeverityEnum `json:"severity"`

	IsEditable bool `json:"is_editable"`

	Description string `json:"description"`
}
