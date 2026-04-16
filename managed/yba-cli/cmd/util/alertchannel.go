/*
 * Copyright (c) YugabyteDB, Inc.
 */

package util

import (
	"encoding/json"

	ybaclient "github.com/yugabyte/platform-go-client"
)

// AlertChannel represents the parameters for an alert channel
type AlertChannel struct {
	// Customer UUID
	CustomerUuid string `json:"customer_uuid"`
	// Name
	Name   string             `json:"name"`
	Params AlertChannelParams `json:"params"`
	// Channel UUID
	Uuid *string `json:"uuid,omitempty"`
}

// AlertChannelFormData represents the parameters for an alert channel
type AlertChannelFormData struct {
	AlertChannelUUID string             `json:"alertChannelUUID"`
	Name             string             `json:"name"`
	Params           AlertChannelParams `json:"params"`
}

// AlertChannelParams represents the parameters for an alert channel
type AlertChannelParams struct {
	// Channel type
	ChannelType *string `json:"channelType,omitempty"`
	// Notification text template
	TextTemplate *string `json:"textTemplate,omitempty"`
	// Notification title template
	TitleTemplate *string `json:"titleTemplate,omitempty"`

	// slack params
	// Slack icon URL
	IconUrl *string `json:"iconUrl,omitempty"`
	// Slack username
	Username string `json:"username"`
	// Slack webhook URL
	WebhookUrl string `json:"webhookUrl"`

	// email params
	// Use health check notification recipients
	DefaultRecipients *bool `json:"defaultRecipients,omitempty"`
	// Use health check notification SMTP settings
	DefaultSmtpSettings *bool `json:"defaultSmtpSettings,omitempty"`
	// List of recipients
	Recipients *[]string           `json:"recipients,omitempty"`
	SmtpData   *ybaclient.SmtpData `json:"smtpData,omitempty"`

	// Webhook params
	HttpAuth *HTTPAuthInformation `json:"httpAuth,omitempty"`
	// WARNING: This is a preview API that could change. Send resolved alert notification
	SendResolved *bool `json:"sendResolved,omitempty"`

	// PagerDuty params
	// API key
	ApiKey string `json:"apiKey"`
	// Routing key
	RoutingKey string `json:"routingKey"`
}

// HTTPAuthInformation represents the parameters for an alert channel
type HTTPAuthInformation struct {
	// Type
	Type *string `json:"type,omitempty"`
	// User name
	Username *string `json:"username,omitempty"`
	// Password
	Password *string `json:"password,omitempty"`
	// Token Header
	TokenHeader *string `json:"tokenHeader,omitempty"`
	// TokenValue
	TokenValue *string `json:"tokenValue,omitempty"`
}

// GetCustomerUuid returns the CustomerUuid field value
func (o *AlertChannel) GetCustomerUuid() string {
	if o == nil {
		var ret string
		return ret
	}

	return o.CustomerUuid
}

// GetCustomerUuidOk returns a tuple with the CustomerUuid field value
// and a boolean to check if the value has been set.
func (o *AlertChannel) GetCustomerUuidOk() (*string, bool) {
	if o == nil {
		return nil, false
	}
	return &o.CustomerUuid, true
}

// SetCustomerUuid sets field value
func (o *AlertChannel) SetCustomerUuid(v string) {
	o.CustomerUuid = v
}

// GetName returns the Name field value
func (o *AlertChannel) GetName() string {
	if o == nil {
		var ret string
		return ret
	}

	return o.Name
}

// GetNameOk returns a tuple with the Name field value
// and a boolean to check if the value has been set.
func (o *AlertChannel) GetNameOk() (*string, bool) {
	if o == nil {
		return nil, false
	}
	return &o.Name, true
}

// SetName sets field value
func (o *AlertChannel) SetName(v string) {
	o.Name = v
}

// GetParams returns the Params field value
func (o *AlertChannel) GetParams() AlertChannelParams {
	if o == nil {
		var ret AlertChannelParams
		return ret
	}

	return o.Params
}

// GetParamsOk returns a tuple with the Params field value
// and a boolean to check if the value has been set.
func (o *AlertChannel) GetParamsOk() (*AlertChannelParams, bool) {
	if o == nil {
		return nil, false
	}
	return &o.Params, true
}

// SetParams sets field value
func (o *AlertChannel) SetParams(v AlertChannelParams) {
	o.Params = v
}

// GetUuid returns the Uuid field value if set, zero value otherwise.
func (o *AlertChannel) GetUuid() string {
	if o == nil || o.Uuid == nil {
		var ret string
		return ret
	}
	return *o.Uuid
}

// GetUuidOk returns a tuple with the Uuid field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *AlertChannel) GetUuidOk() (*string, bool) {
	if o == nil || o.Uuid == nil {
		return nil, false
	}
	return o.Uuid, true
}

// HasUuid returns a boolean if a field has been set.
func (o *AlertChannel) HasUuid() bool {
	if o != nil && o.Uuid != nil {
		return true
	}

	return false
}

// SetUuid gets a reference to the given string and assigns it to the Uuid field.
func (o *AlertChannel) SetUuid(v string) {
	o.Uuid = &v
}

// MarshalJSON serializes the struct using a buffer.
func (o AlertChannel) MarshalJSON() ([]byte, error) {
	toSerialize := map[string]interface{}{}
	if true {
		toSerialize["customer_uuid"] = o.CustomerUuid
	}
	if true {
		toSerialize["name"] = o.Name
	}
	if true {
		toSerialize["params"] = o.Params
	}
	if o.Uuid != nil {
		toSerialize["uuid"] = o.Uuid
	}
	return json.Marshal(toSerialize)
}

// GetAlertChannelUUID returns the AlertChannelUUID field value
func (o *AlertChannelFormData) GetAlertChannelUUID() string {
	if o == nil {
		var ret string
		return ret
	}

	return o.AlertChannelUUID
}

// GetAlertChannelUUIDOk returns a tuple with the AlertChannelUUID field value
// and a boolean to check if the value has been set.
func (o *AlertChannelFormData) GetAlertChannelUUIDOk() (*string, bool) {
	if o == nil {
		return nil, false
	}
	return &o.AlertChannelUUID, true
}

// SetAlertChannelUUID sets field value
func (o *AlertChannelFormData) SetAlertChannelUUID(v string) {
	o.AlertChannelUUID = v
}

// GetName returns the Name field value
func (o *AlertChannelFormData) GetName() string {
	if o == nil {
		var ret string
		return ret
	}

	return o.Name
}

// GetNameOk returns a tuple with the Name field value
// and a boolean to check if the value has been set.
func (o *AlertChannelFormData) GetNameOk() (*string, bool) {
	if o == nil {
		return nil, false
	}
	return &o.Name, true
}

// SetName sets field value
func (o *AlertChannelFormData) SetName(v string) {
	o.Name = v
}

// GetParams returns the Params field value
func (o *AlertChannelFormData) GetParams() AlertChannelParams {
	if o == nil {
		var ret AlertChannelParams
		return ret
	}

	return o.Params
}

// GetParamsOk returns a tuple with the Params field value
// and a boolean to check if the value has been set.
func (o *AlertChannelFormData) GetParamsOk() (*AlertChannelParams, bool) {
	if o == nil {
		return nil, false
	}
	return &o.Params, true
}

// SetParams sets field value
func (o *AlertChannelFormData) SetParams(v AlertChannelParams) {
	o.Params = v
}

// MarshalJSON serializes the struct using a buffer.
func (o AlertChannelFormData) MarshalJSON() ([]byte, error) {
	toSerialize := map[string]interface{}{}
	if true {
		toSerialize["alertChannelUUID"] = o.AlertChannelUUID
	}
	if true {
		toSerialize["name"] = o.Name
	}
	if true {
		toSerialize["params"] = o.Params
	}
	return json.Marshal(toSerialize)
}

// GetChannelType returns the ChannelType field value if set, zero value otherwise.
func (o *AlertChannelParams) GetChannelType() string {
	if o == nil || o.ChannelType == nil {
		var ret string
		return ret
	}
	return *o.ChannelType
}

// GetChannelTypeOk returns a tuple with the ChannelType field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *AlertChannelParams) GetChannelTypeOk() (*string, bool) {
	if o == nil || o.ChannelType == nil {
		return nil, false
	}
	return o.ChannelType, true
}

// HasChannelType returns a boolean if a field has been set.
func (o *AlertChannelParams) HasChannelType() bool {
	if o != nil && o.ChannelType != nil {
		return true
	}

	return false
}

// SetChannelType gets a reference to the given string and assigns it to the ChannelType field.
func (o *AlertChannelParams) SetChannelType(v string) {
	o.ChannelType = &v
}

// GetTextTemplate returns the TextTemplate field value if set, zero value otherwise.
func (o *AlertChannelParams) GetTextTemplate() string {
	if o == nil || o.TextTemplate == nil {
		var ret string
		return ret
	}
	return *o.TextTemplate
}

// GetTextTemplateOk returns a tuple with the TextTemplate field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *AlertChannelParams) GetTextTemplateOk() (*string, bool) {
	if o == nil || o.TextTemplate == nil {
		return nil, false
	}
	return o.TextTemplate, true
}

// HasTextTemplate returns a boolean if a field has been set.
func (o *AlertChannelParams) HasTextTemplate() bool {
	if o != nil && o.TextTemplate != nil {
		return true
	}

	return false
}

// SetTextTemplate gets a reference to the given string and assigns it to the TextTemplate field.
func (o *AlertChannelParams) SetTextTemplate(v string) {
	o.TextTemplate = &v
}

// GetTitleTemplate returns the TitleTemplate field value if set, zero value otherwise.
func (o *AlertChannelParams) GetTitleTemplate() string {
	if o == nil || o.TitleTemplate == nil {
		var ret string
		return ret
	}
	return *o.TitleTemplate
}

// GetTitleTemplateOk returns a tuple with the TitleTemplate field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *AlertChannelParams) GetTitleTemplateOk() (*string, bool) {
	if o == nil || o.TitleTemplate == nil {
		return nil, false
	}
	return o.TitleTemplate, true
}

// HasTitleTemplate returns a boolean if a field has been set.
func (o *AlertChannelParams) HasTitleTemplate() bool {
	if o != nil && o.TitleTemplate != nil {
		return true
	}

	return false
}

// SetTitleTemplate gets a reference to the given string and assigns it to the TitleTemplate field.
func (o *AlertChannelParams) SetTitleTemplate(v string) {
	o.TitleTemplate = &v
}

// GetIconUrl returns the IconUrl field value if set, zero value otherwise.
func (o *AlertChannelParams) GetIconUrl() string {
	if o == nil || o.IconUrl == nil {
		var ret string
		return ret
	}
	return *o.IconUrl
}

// GetIconUrlOk returns a tuple with the IconUrl field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *AlertChannelParams) GetIconUrlOk() (*string, bool) {
	if o == nil || o.IconUrl == nil {
		return nil, false
	}
	return o.IconUrl, true
}

// HasIconUrl returns a boolean if a field has been set.
func (o *AlertChannelParams) HasIconUrl() bool {
	if o != nil && o.IconUrl != nil {
		return true
	}

	return false
}

// SetIconUrl gets a reference to the given string and assigns it to the IconUrl field.
func (o *AlertChannelParams) SetIconUrl(v string) {
	o.IconUrl = &v
}

// GetUsername returns the Username field value
func (o *AlertChannelParams) GetUsername() string {
	if o == nil {
		var ret string
		return ret
	}

	return o.Username
}

// GetUsernameOk returns a tuple with the Username field value
// and a boolean to check if the value has been set.
func (o *AlertChannelParams) GetUsernameOk() (*string, bool) {
	if o == nil {
		return nil, false
	}
	return &o.Username, true
}

// SetUsername sets field value
func (o *AlertChannelParams) SetUsername(v string) {
	o.Username = v
}

// GetWebhookUrl returns the WebhookUrl field value
func (o *AlertChannelParams) GetWebhookUrl() string {
	if o == nil {
		var ret string
		return ret
	}

	return o.WebhookUrl
}

// GetWebhookUrlOk returns a tuple with the WebhookUrl field value
// and a boolean to check if the value has been set.
func (o *AlertChannelParams) GetWebhookUrlOk() (*string, bool) {
	if o == nil {
		return nil, false
	}
	return &o.WebhookUrl, true
}

// SetWebhookUrl sets field value
func (o *AlertChannelParams) SetWebhookUrl(v string) {
	o.WebhookUrl = v
}

// GetDefaultRecipients returns the DefaultRecipients field value if set, zero value otherwise.
func (o *AlertChannelParams) GetDefaultRecipients() bool {
	if o == nil || o.DefaultRecipients == nil {
		var ret bool
		return ret
	}
	return *o.DefaultRecipients
}

// GetDefaultRecipientsOk returns a tuple with the DefaultRecipients field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *AlertChannelParams) GetDefaultRecipientsOk() (*bool, bool) {
	if o == nil || o.DefaultRecipients == nil {
		return nil, false
	}
	return o.DefaultRecipients, true
}

// HasDefaultRecipients returns a boolean if a field has been set.
func (o *AlertChannelParams) HasDefaultRecipients() bool {
	if o != nil && o.DefaultRecipients != nil {
		return true
	}

	return false
}

// SetDefaultRecipients gets a reference to the given bool and assigns it to the DefaultRecipients field.
func (o *AlertChannelParams) SetDefaultRecipients(v bool) {
	o.DefaultRecipients = &v
}

// GetDefaultSmtpSettings returns the DefaultSmtpSettings field value if set, zero value otherwise.
func (o *AlertChannelParams) GetDefaultSmtpSettings() bool {
	if o == nil || o.DefaultSmtpSettings == nil {
		var ret bool
		return ret
	}
	return *o.DefaultSmtpSettings
}

// GetDefaultSmtpSettingsOk returns a tuple with the DefaultSmtpSettings field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *AlertChannelParams) GetDefaultSmtpSettingsOk() (*bool, bool) {
	if o == nil || o.DefaultSmtpSettings == nil {
		return nil, false
	}
	return o.DefaultSmtpSettings, true
}

// HasDefaultSmtpSettings returns a boolean if a field has been set.
func (o *AlertChannelParams) HasDefaultSmtpSettings() bool {
	if o != nil && o.DefaultSmtpSettings != nil {
		return true
	}

	return false
}

// SetDefaultSmtpSettings gets a reference to the given bool and assigns it to the DefaultSmtpSettings field.
func (o *AlertChannelParams) SetDefaultSmtpSettings(v bool) {
	o.DefaultSmtpSettings = &v
}

// GetRecipients returns the Recipients field value if set, zero value otherwise.
func (o *AlertChannelParams) GetRecipients() []string {
	if o == nil || o.Recipients == nil {
		var ret []string
		return ret
	}
	return *o.Recipients
}

// GetRecipientsOk returns a tuple with the Recipients field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *AlertChannelParams) GetRecipientsOk() (*[]string, bool) {
	if o == nil || o.Recipients == nil {
		return nil, false
	}
	return o.Recipients, true
}

// HasRecipients returns a boolean if a field has been set.
func (o *AlertChannelParams) HasRecipients() bool {
	if o != nil && o.Recipients != nil {
		return true
	}

	return false
}

// SetRecipients gets a reference to the given []string and assigns it to the Recipients field.
func (o *AlertChannelParams) SetRecipients(v []string) {
	o.Recipients = &v
}

// GetSmtpData returns the SmtpData field value if set, zero value otherwise.
func (o *AlertChannelParams) GetSmtpData() ybaclient.SmtpData {
	if o == nil || o.SmtpData == nil {
		var ret ybaclient.SmtpData
		return ret
	}
	return *o.SmtpData
}

// GetSmtpDataOk returns a tuple with the SmtpData field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *AlertChannelParams) GetSmtpDataOk() (*ybaclient.SmtpData, bool) {
	if o == nil || o.SmtpData == nil {
		return nil, false
	}
	return o.SmtpData, true
}

// HasSmtpData returns a boolean if a field has been set.
func (o *AlertChannelParams) HasSmtpData() bool {
	if o != nil && o.SmtpData != nil {
		return true
	}

	return false
}

// GetHttpAuth returns the HttpAuth field value if set, zero value otherwise.
func (o *AlertChannelParams) GetHttpAuth() HTTPAuthInformation {
	if o == nil || o.HttpAuth == nil {
		var ret HTTPAuthInformation
		return ret
	}
	return *o.HttpAuth
}

// GetHttpAuthOk returns a tuple with the HttpAuth field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *AlertChannelParams) GetHttpAuthOk() (*HTTPAuthInformation, bool) {
	if o == nil || o.HttpAuth == nil {
		return nil, false
	}
	return o.HttpAuth, true
}

// HasHttpAuth returns a boolean if a field has been set.
func (o *AlertChannelParams) HasHttpAuth() bool {
	if o != nil && o.HttpAuth != nil {
		return true
	}

	return false
}

// SetHttpAuth gets a reference to the given HTTPAuthInformation and assigns it to the HttpAuth field.
func (o *AlertChannelParams) SetHttpAuth(v HTTPAuthInformation) {
	o.HttpAuth = &v
}

// GetSendResolved returns the SendResolved field value if set, zero value otherwise.
func (o *AlertChannelParams) GetSendResolved() bool {
	if o == nil || o.SendResolved == nil {
		var ret bool
		return ret
	}
	return *o.SendResolved
}

// GetSendResolvedOk returns a tuple with the SendResolved field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *AlertChannelParams) GetSendResolvedOk() (*bool, bool) {
	if o == nil || o.SendResolved == nil {
		return nil, false
	}
	return o.SendResolved, true
}

// HasSendResolved returns a boolean if a field has been set.
func (o *AlertChannelParams) HasSendResolved() bool {
	if o != nil && o.SendResolved != nil {
		return true
	}

	return false
}

// SetSendResolved gets a reference to the given bool and assigns it to the SendResolved field.
func (o *AlertChannelParams) SetSendResolved(v bool) {
	o.SendResolved = &v
}

// SetSmtpData gets a reference to the given SmtpData and assigns it to the SmtpData field.
func (o *AlertChannelParams) SetSmtpData(v ybaclient.SmtpData) {
	o.SmtpData = &v
}

// GetApiKey returns the ApiKey field value
func (o *AlertChannelParams) GetApiKey() string {
	if o == nil {
		var ret string
		return ret
	}

	return o.ApiKey
}

// GetApiKeyOk returns a tuple with the ApiKey field value
// and a boolean to check if the value has been set.
func (o *AlertChannelParams) GetApiKeyOk() (*string, bool) {
	if o == nil {
		return nil, false
	}
	return &o.ApiKey, true
}

// SetApiKey sets field value
func (o *AlertChannelParams) SetApiKey(v string) {
	o.ApiKey = v
}

// GetRoutingKey returns the RoutingKey field value
func (o *AlertChannelParams) GetRoutingKey() string {
	if o == nil {
		var ret string
		return ret
	}

	return o.RoutingKey
}

// GetRoutingKeyOk returns a tuple with the RoutingKey field value
// and a boolean to check if the value has been set.
func (o *AlertChannelParams) GetRoutingKeyOk() (*string, bool) {
	if o == nil {
		return nil, false
	}
	return &o.RoutingKey, true
}

// SetRoutingKey sets field value
func (o *AlertChannelParams) SetRoutingKey(v string) {
	o.RoutingKey = v
}

// MarshalJSON serializes the struct using a buffer.
func (o AlertChannelParams) MarshalJSON() ([]byte, error) {
	toSerialize := map[string]interface{}{}
	if o.ChannelType != nil {
		toSerialize["channelType"] = o.ChannelType
	}
	if o.TextTemplate != nil {
		toSerialize["textTemplate"] = o.TextTemplate
	}
	if o.TitleTemplate != nil {
		toSerialize["titleTemplate"] = o.TitleTemplate
	}
	if o.IconUrl != nil {
		toSerialize["iconUrl"] = o.IconUrl
	}
	if o.Username != "" {
		toSerialize["username"] = o.Username
	}
	if o.WebhookUrl != "" {
		toSerialize["webhookUrl"] = o.WebhookUrl
	}
	if o.DefaultRecipients != nil {
		toSerialize["defaultRecipients"] = o.DefaultRecipients
	}
	if o.DefaultSmtpSettings != nil {
		toSerialize["defaultSmtpSettings"] = o.DefaultSmtpSettings
	}
	if o.Recipients != nil {
		toSerialize["recipients"] = o.Recipients
	}
	if o.SmtpData != nil {
		toSerialize["smtpData"] = o.SmtpData
	}
	if o.HttpAuth != nil {
		toSerialize["httpAuth"] = o.HttpAuth
	}
	if o.SendResolved != nil {
		toSerialize["sendResolved"] = o.SendResolved
	}
	if o.ApiKey != "" {
		toSerialize["apiKey"] = o.ApiKey
	}
	if o.RoutingKey != "" {
		toSerialize["routingKey"] = o.RoutingKey
	}
	return json.Marshal(toSerialize)
}

// GetType of HTTP Auth
func (o *HTTPAuthInformation) GetType() string {
	if o == nil || o.Type == nil {
		var ret string
		return ret
	}

	return *o.Type
}

// GetTypeOk returns a tuple with the Type field value
// and a boolean to check if the value has been set.
func (o *HTTPAuthInformation) GetTypeOk() (*string, bool) {
	if o == nil || o.Type == nil {
		return nil, false
	}
	return o.Type, true
}

// SetType sets field value
func (o *HTTPAuthInformation) SetType(v string) {
	o.Type = &v
}

// GetUsername of HTTP Auth
func (o *HTTPAuthInformation) GetUsername() string {
	if o == nil || o.Username == nil {
		var ret string
		return ret
	}
	return *o.Username
}

// GetUsernameOk returns a tuple with the Username field value
// and a boolean to check if the value has been set.
func (o *HTTPAuthInformation) GetUsernameOk() (*string, bool) {
	if o == nil || o.Username == nil {
		return nil, false
	}
	return o.Username, true
}

// SetUsername sets field value
func (o *HTTPAuthInformation) SetUsername(v string) {
	o.Username = &v
}

// GetPassword of HTTP Auth
func (o *HTTPAuthInformation) GetPassword() string {
	if o == nil || o.Password == nil {
		var ret string
		return ret
	}
	return *o.Password
}

// GetPasswordOk returns a tuple with the Password field value
// and a boolean to check if the value has been set.
func (o *HTTPAuthInformation) GetPasswordOk() (*string, bool) {
	if o == nil {
		return nil, false
	}
	return o.Password, true
}

// SetPassword sets field value
func (o *HTTPAuthInformation) SetPassword(v string) {
	o.Password = &v
}

// GetTokenHeader of HTTP Auth
func (o *HTTPAuthInformation) GetTokenHeader() string {
	if o == nil || o.TokenHeader == nil {
		var ret string
		return ret
	}
	return *o.TokenHeader
}

// GetTokenHeaderOk returns a tuple with the TokenHeader field value
// and a boolean to check if the value has been set.
func (o *HTTPAuthInformation) GetTokenHeaderOk() (*string, bool) {
	if o == nil || o.TokenHeader == nil {
		return nil, false
	}
	return o.TokenHeader, true
}

// SetTokenHeader sets field value
func (o *HTTPAuthInformation) SetTokenHeader(v string) {
	o.TokenHeader = &v
}

// GetTokenValue returns TokenValue of HTTP Auth
func (o *HTTPAuthInformation) GetTokenValue() string {
	if o == nil || o.TokenValue == nil {
		var ret string
		return ret
	}
	return *o.TokenValue
}

// GetTokenValueOk returns a tuple with the TokenValue field value
// and a boolean to check if the value has been set.
func (o *HTTPAuthInformation) GetTokenValueOk() (*string, bool) {
	if o == nil || o.TokenValue == nil {
		return nil, false
	}
	return o.TokenValue, true
}

// SetTokenValue sets field value
func (o *HTTPAuthInformation) SetTokenValue(v string) {
	o.TokenValue = &v
}

// MarshalJSON serializes the struct using a buffer.
func (o HTTPAuthInformation) MarshalJSON() ([]byte, error) {
	toSerialize := map[string]interface{}{}
	if o.Type != nil {
		toSerialize["type"] = o.Type
	}
	if o.Username != nil {
		toSerialize["username"] = o.Username
	}
	if o.Password != nil {
		toSerialize["password"] = o.Password
	}
	if o.TokenHeader != nil {
		toSerialize["tokenHeader"] = o.TokenHeader
	}
	if o.TokenValue != nil {
		toSerialize["tokenValue"] = o.TokenValue
	}
	return json.Marshal(toSerialize)
}
