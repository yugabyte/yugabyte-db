/*
 * Copyright (c) YugabyteDB, Inc.
 */

package util

import (
	"encoding/json"
	"time"

	ybaclient "github.com/yugabyte/platform-go-client"
)

// TelemetryProvider Telemetry Provider Model
type TelemetryProvider struct {
	Config TelemetryProviderConfig `json:"config"`
	// Creation timestamp
	CreateTime *time.Time `json:"createTime,omitempty"`
	// Customer UUID
	CustomerUUID string `json:"customerUUID"`
	// Name
	Name string `json:"name"`
	// Extra Tags
	Tags map[string]string `json:"tags"`
	// Updation timestamp
	UpdateTime *time.Time `json:"updateTime,omitempty"`
	// Telemetry Provider UUID
	Uuid string `json:"uuid"`
}

// TelemetryProviderConfig Telemetry Provider Config
type TelemetryProviderConfig struct {
	// Telemetry Provider Type
	Type *string `json:"type,omitempty"`

	// AWSCloudWatch config
	// Access Key
	AccessKey string `json:"accessKey"`
	// End Point
	Endpoint *string `json:"endpoint,omitempty"`
	// Log Group
	LogGroup string `json:"logGroup"`
	// Log Stream
	LogStream string `json:"logStream"`
	// Region
	Region string `json:"region"`
	// Role ARN
	RoleARN *string `json:"roleARN,omitempty"`
	// Secret Key
	SecretKey string `json:"secretKey"`

	// GCPCloudMonitoring
	// Project ID
	Project *string `json:"project,omitempty"`
	// Credentials
	// Credentials map[string]interface{} `json:"credentials"`

	// Datadog
	// API Key
	ApiKey *string `json:"apiKey,omitempty"`
	// Site
	Site *string `json:"site,omitempty"`

	// Splunk
	// Index
	Index *string `json:"index,omitempty"`
	// Source
	Source *string `json:"source,omitempty"`
	// Source Type
	SourceType *string `json:"sourceType,omitempty"`
	// Token
	Token *string `json:"token,omitempty"`

	// Loki
	// Auth Type
	AuthType  string                          `json:"authType"`
	BasicAuth *ybaclient.BasicAuthCredentials `json:"basicAuth,omitempty"`
	// Organization/Tenant ID
	OrganizationID *string `json:"organizationID,omitempty"`
}

// NewTelemetryProvider instantiates a new TelemetryProvider object
// This constructor will assign default values to properties that have it defined,
// and makes sure properties required by API are set, but the set of arguments
// will change when the set of required properties is changed
func NewTelemetryProvider(
	config TelemetryProviderConfig,
	customerUUID string,
	name string,
	tags map[string]string,
	uuid string,
) *TelemetryProvider {
	this := TelemetryProvider{}
	this.Config = config
	this.CustomerUUID = customerUUID
	this.Name = name
	this.Tags = tags
	this.Uuid = uuid
	return &this
}

// NewTelemetryProviderWithDefaults instantiates a new TelemetryProvider object
// This constructor will only assign default values to properties that have it defined,
// but it doesn't guarantee that properties required by API are set
func NewTelemetryProviderWithDefaults() *TelemetryProvider {
	this := TelemetryProvider{}
	return &this
}

// GetConfig returns the Config field value
func (o *TelemetryProvider) GetConfig() TelemetryProviderConfig {
	if o == nil {
		var ret TelemetryProviderConfig
		return ret
	}

	return o.Config
}

// GetConfigOk returns a tuple with the Config field value
// and a boolean to check if the value has been set.
func (o *TelemetryProvider) GetConfigOk() (*TelemetryProviderConfig, bool) {
	if o == nil {
		return nil, false
	}
	return &o.Config, true
}

// SetConfig sets field value
func (o *TelemetryProvider) SetConfig(v TelemetryProviderConfig) {
	o.Config = v
}

// GetCreateTime returns the CreateTime field value if set, zero value otherwise.
func (o *TelemetryProvider) GetCreateTime() time.Time {
	if o == nil || o.CreateTime == nil {
		var ret time.Time
		return ret
	}
	return *o.CreateTime
}

// GetCreateTimeOk returns a tuple with the CreateTime field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *TelemetryProvider) GetCreateTimeOk() (*time.Time, bool) {
	if o == nil || o.CreateTime == nil {
		return nil, false
	}
	return o.CreateTime, true
}

// HasCreateTime returns a boolean if a field has been set.
func (o *TelemetryProvider) HasCreateTime() bool {
	if o != nil && o.CreateTime != nil {
		return true
	}

	return false
}

// SetCreateTime gets a reference to the given time.Time and assigns it to the CreateTime field.
func (o *TelemetryProvider) SetCreateTime(v time.Time) {
	o.CreateTime = &v
}

// GetCustomerUUID returns the CustomerUUID field value
func (o *TelemetryProvider) GetCustomerUUID() string {
	if o == nil {
		var ret string
		return ret
	}

	return o.CustomerUUID
}

// GetCustomerUUIDOk returns a tuple with the CustomerUUID field value
// and a boolean to check if the value has been set.
func (o *TelemetryProvider) GetCustomerUUIDOk() (*string, bool) {
	if o == nil {
		return nil, false
	}
	return &o.CustomerUUID, true
}

// SetCustomerUUID sets field value
func (o *TelemetryProvider) SetCustomerUUID(v string) {
	o.CustomerUUID = v
}

// GetName returns the Name field value
func (o *TelemetryProvider) GetName() string {
	if o == nil {
		var ret string
		return ret
	}

	return o.Name
}

// GetNameOk returns a tuple with the Name field value
// and a boolean to check if the value has been set.
func (o *TelemetryProvider) GetNameOk() (*string, bool) {
	if o == nil {
		return nil, false
	}
	return &o.Name, true
}

// SetName sets field value
func (o *TelemetryProvider) SetName(v string) {
	o.Name = v
}

// GetTags returns the Tags field value
func (o *TelemetryProvider) GetTags() map[string]string {
	if o == nil {
		var ret map[string]string
		return ret
	}

	return o.Tags
}

// GetTagsOk returns a tuple with the Tags field value
// and a boolean to check if the value has been set.
func (o *TelemetryProvider) GetTagsOk() (*map[string]string, bool) {
	if o == nil {
		return nil, false
	}
	return &o.Tags, true
}

// SetTags sets field value
func (o *TelemetryProvider) SetTags(v map[string]string) {
	o.Tags = v
}

// GetUpdateTime returns the UpdateTime field value if set, zero value otherwise.
func (o *TelemetryProvider) GetUpdateTime() time.Time {
	if o == nil || o.UpdateTime == nil {
		var ret time.Time
		return ret
	}
	return *o.UpdateTime
}

// GetUpdateTimeOk returns a tuple with the UpdateTime field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *TelemetryProvider) GetUpdateTimeOk() (*time.Time, bool) {
	if o == nil || o.UpdateTime == nil {
		return nil, false
	}
	return o.UpdateTime, true
}

// HasUpdateTime returns a boolean if a field has been set.
func (o *TelemetryProvider) HasUpdateTime() bool {
	if o != nil && o.UpdateTime != nil {
		return true
	}

	return false
}

// SetUpdateTime gets a reference to the given time.Time and assigns it to the UpdateTime field.
func (o *TelemetryProvider) SetUpdateTime(v time.Time) {
	o.UpdateTime = &v
}

// GetUuid returns the Uuid field value
func (o *TelemetryProvider) GetUuid() string {
	if o == nil {
		var ret string
		return ret
	}

	return o.Uuid
}

// GetUuidOk returns a tuple with the Uuid field value
// and a boolean to check if the value has been set.
func (o *TelemetryProvider) GetUuidOk() (*string, bool) {
	if o == nil {
		return nil, false
	}
	return &o.Uuid, true
}

// SetUuid sets field value
func (o *TelemetryProvider) SetUuid(v string) {
	o.Uuid = v
}

// MarshalJSON serializes the struct using spec logic.
func (o TelemetryProvider) MarshalJSON() ([]byte, error) {
	toSerialize := map[string]interface{}{}
	if true {
		toSerialize["config"] = o.Config
	}
	if o.CreateTime != nil {
		toSerialize["createTime"] = o.CreateTime
	}
	if true {
		toSerialize["customerUUID"] = o.CustomerUUID
	}
	if true {
		toSerialize["name"] = o.Name
	}
	if true {
		toSerialize["tags"] = o.Tags
	}
	if o.UpdateTime != nil {
		toSerialize["updateTime"] = o.UpdateTime
	}
	if true {
		toSerialize["uuid"] = o.Uuid
	}
	return json.Marshal(toSerialize)
}

// NullableTelemetryProvider is a helper abstraction for handling nullable telemetryprovider types.
type NullableTelemetryProvider struct {
	value *TelemetryProvider
	isSet bool
}

// Get returns the value
func (v NullableTelemetryProvider) Get() *TelemetryProvider {
	return v.value
}

// Set modifies the value
func (v *NullableTelemetryProvider) Set(val *TelemetryProvider) {
	v.value = val
	v.isSet = true
}

// IsSet returns true if the value is set.
func (v NullableTelemetryProvider) IsSet() bool {
	return v.isSet
}

// Unset removes the value
func (v *NullableTelemetryProvider) Unset() {
	v.value = nil
	v.isSet = false
}

// NewNullableTelemetryProvider instantiates a new NullableTelemetryProvider object
func NewNullableTelemetryProvider(val *TelemetryProvider) *NullableTelemetryProvider {
	return &NullableTelemetryProvider{value: val, isSet: true}
}

// MarshalJSON implements the json.Marshaler interface.
// This constructor will only assign a value to isSet, has no default value.
func (v NullableTelemetryProvider) MarshalJSON() ([]byte, error) {
	return json.Marshal(v.value)
}

// UnmarshalJSON implements the Unmarshaler interface.
func (v *NullableTelemetryProvider) UnmarshalJSON(src []byte) error {
	v.isSet = true
	return json.Unmarshal(src, &v.value)
}

// NewTelemetryProviderConfigWithDefaults instantiates a new TelemetryProviderConfig object
// This constructor will only assign default values to properties that have it defined,
// but it doesn't guarantee that properties required by API are set
func NewTelemetryProviderConfigWithDefaults() *TelemetryProviderConfig {
	this := TelemetryProviderConfig{}
	return &this
}

// GetType returns the Type field value if set, zero value otherwise.
func (o *TelemetryProviderConfig) GetType() string {
	if o == nil || o.Type == nil {
		var ret string
		return ret
	}
	return *o.Type
}

// GetTypeOk returns a tuple with the Type field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *TelemetryProviderConfig) GetTypeOk() (*string, bool) {
	if o == nil || o.Type == nil {
		return nil, false
	}
	return o.Type, true
}

// HasType returns a boolean if a field has been set.
func (o *TelemetryProviderConfig) HasType() bool {
	if o != nil && o.Type != nil {
		return true
	}

	return false
}

// SetType gets a reference to the given string and assigns it to the Type field.
func (o *TelemetryProviderConfig) SetType(v string) {
	o.Type = &v
}

// GetAccessKey returns the AccessKey field value
func (o *TelemetryProviderConfig) GetAccessKey() string {
	if o == nil {
		var ret string
		return ret
	}

	return o.AccessKey
}

// GetAccessKeyOk returns a tuple with the AccessKey field value
// and a boolean to check if the value has been set.
func (o *TelemetryProviderConfig) GetAccessKeyOk() (*string, bool) {
	if o == nil {
		return nil, false
	}
	return &o.AccessKey, true
}

// SetAccessKey sets field value
func (o *TelemetryProviderConfig) SetAccessKey(v string) {
	o.AccessKey = v
}

// GetEndpoint returns the Endpoint field value if set, zero value otherwise.
func (o *TelemetryProviderConfig) GetEndpoint() string {
	if o == nil || o.Endpoint == nil {
		var ret string
		return ret
	}
	return *o.Endpoint
}

// GetEndpointOk returns a tuple with the Endpoint field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *TelemetryProviderConfig) GetEndpointOk() (*string, bool) {
	if o == nil || o.Endpoint == nil {
		return nil, false
	}
	return o.Endpoint, true
}

// HasEndpoint returns a boolean if a field has been set.
func (o *TelemetryProviderConfig) HasEndpoint() bool {
	if o != nil && o.Endpoint != nil {
		return true
	}

	return false
}

// SetEndpoint gets a reference to the given string and assigns it to the Endpoint field.
func (o *TelemetryProviderConfig) SetEndpoint(v string) {
	o.Endpoint = &v
}

// GetLogGroup returns the LogGroup field value
func (o *TelemetryProviderConfig) GetLogGroup() string {
	if o == nil {
		var ret string
		return ret
	}

	return o.LogGroup
}

// GetLogGroupOk returns a tuple with the LogGroup field value
// and a boolean to check if the value has been set.
func (o *TelemetryProviderConfig) GetLogGroupOk() (*string, bool) {
	if o == nil {
		return nil, false
	}
	return &o.LogGroup, true
}

// SetLogGroup sets field value
func (o *TelemetryProviderConfig) SetLogGroup(v string) {
	o.LogGroup = v
}

// GetLogStream returns the LogStream field value
func (o *TelemetryProviderConfig) GetLogStream() string {
	if o == nil {
		var ret string
		return ret
	}

	return o.LogStream
}

// GetLogStreamOk returns a tuple with the LogStream field value
// and a boolean to check if the value has been set.
func (o *TelemetryProviderConfig) GetLogStreamOk() (*string, bool) {
	if o == nil {
		return nil, false
	}
	return &o.LogStream, true
}

// SetLogStream sets field value
func (o *TelemetryProviderConfig) SetLogStream(v string) {
	o.LogStream = v
}

// GetRegion returns the Region field value
func (o *TelemetryProviderConfig) GetRegion() string {
	if o == nil {
		var ret string
		return ret
	}

	return o.Region
}

// GetRegionOk returns a tuple with the Region field value
// and a boolean to check if the value has been set.
func (o *TelemetryProviderConfig) GetRegionOk() (*string, bool) {
	if o == nil {
		return nil, false
	}
	return &o.Region, true
}

// SetRegion sets field value
func (o *TelemetryProviderConfig) SetRegion(v string) {
	o.Region = v
}

// GetRoleARN returns the RoleARN field value if set, zero value otherwise.
func (o *TelemetryProviderConfig) GetRoleARN() string {
	if o == nil || o.RoleARN == nil {
		var ret string
		return ret
	}
	return *o.RoleARN
}

// GetRoleARNOk returns a tuple with the RoleARN field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *TelemetryProviderConfig) GetRoleARNOk() (*string, bool) {
	if o == nil || o.RoleARN == nil {
		return nil, false
	}
	return o.RoleARN, true
}

// HasRoleARN returns a boolean if a field has been set.
func (o *TelemetryProviderConfig) HasRoleARN() bool {
	if o != nil && o.RoleARN != nil {
		return true
	}

	return false
}

// SetRoleARN gets a reference to the given string and assigns it to the RoleARN field.
func (o *TelemetryProviderConfig) SetRoleARN(v string) {
	o.RoleARN = &v
}

// GetSecretKey returns the SecretKey field value
func (o *TelemetryProviderConfig) GetSecretKey() string {
	if o == nil {
		var ret string
		return ret
	}

	return o.SecretKey
}

// GetSecretKeyOk returns a tuple with the SecretKey field value
// and a boolean to check if the value has been set.
func (o *TelemetryProviderConfig) GetSecretKeyOk() (*string, bool) {
	if o == nil {
		return nil, false
	}
	return &o.SecretKey, true
}

// SetSecretKey sets field value
func (o *TelemetryProviderConfig) SetSecretKey(v string) {
	o.SecretKey = v
}

// GetProject returns the Project field value if set, zero value otherwise.
func (o *TelemetryProviderConfig) GetProject() string {
	if o == nil || o.Project == nil {
		var ret string
		return ret
	}
	return *o.Project
}

// GetProjectOk returns a tuple with the Project field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *TelemetryProviderConfig) GetProjectOk() (*string, bool) {
	if o == nil || o.Project == nil {
		return nil, false
	}
	return o.Project, true
}

// HasProject returns a boolean if a field has been set.
func (o *TelemetryProviderConfig) HasProject() bool {
	if o != nil && o.Project != nil {
		return true
	}

	return false
}

// SetProject gets a reference to the given string and assigns it to the Project field.
func (o *TelemetryProviderConfig) SetProject(v string) {
	o.Project = &v
}

// // GetCredentials returns the Credentials field value
// func (o *TelemetryProviderConfig) GetCredentials() map[string]interface{} {
// 	if o == nil {
// 		var ret map[string]interface{}
// 		return ret
// 	}
// 	return o.Credentials
// }

// // GetCredentialsOk returns a tuple with the Credentials field value
// // and a boolean to check if the value has been set.
// func (o *TelemetryProviderConfig) GetCredentialsOk() (*map[string]interface{}, bool) {
// 	if o == nil {
// 		return nil, false
// 	}
// 	return &o.Credentials, true
// }

// // SetCredentials sets field value
// func (o *TelemetryProviderConfig) SetCredentials(v map[string]interface{}) {
// 	o.Credentials = v
// }

// GetApiKey returns the ApiKey field value if set, zero value otherwise.
func (o *TelemetryProviderConfig) GetApiKey() string {
	if o == nil || o.ApiKey == nil {
		var ret string
		return ret
	}
	return *o.ApiKey
}

// GetApiKeyOk returns a tuple with the ApiKey field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *TelemetryProviderConfig) GetApiKeyOk() (*string, bool) {
	if o == nil || o.ApiKey == nil {
		return nil, false
	}
	return o.ApiKey, true
}

// HasApiKey returns a boolean if a field has been set.
func (o *TelemetryProviderConfig) HasApiKey() bool {
	if o != nil && o.ApiKey != nil {
		return true
	}

	return false
}

// SetApiKey gets a reference to the given string and assigns it to the ApiKey field.
func (o *TelemetryProviderConfig) SetApiKey(v string) {
	o.ApiKey = &v
}

// GetSite returns the Site field value if set, zero value otherwise.
func (o *TelemetryProviderConfig) GetSite() string {
	if o == nil || o.Site == nil {
		var ret string
		return ret
	}
	return *o.Site
}

// GetSiteOk returns a tuple with the Site field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *TelemetryProviderConfig) GetSiteOk() (*string, bool) {
	if o == nil || o.Site == nil {
		return nil, false
	}
	return o.Site, true
}

// HasSite returns a boolean if a field has been set.
func (o *TelemetryProviderConfig) HasSite() bool {
	if o != nil && o.Site != nil {
		return true
	}

	return false
}

// SetSite gets a reference to the given string and assigns it to the Site field.
func (o *TelemetryProviderConfig) SetSite(v string) {
	o.Site = &v
}

// GetIndex returns the Index field value if set, zero value otherwise.
func (o *TelemetryProviderConfig) GetIndex() string {
	if o == nil || o.Index == nil {
		var ret string
		return ret
	}
	return *o.Index
}

// GetIndexOk returns a tuple with the Index field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *TelemetryProviderConfig) GetIndexOk() (*string, bool) {
	if o == nil || o.Index == nil {
		return nil, false
	}
	return o.Index, true
}

// HasIndex returns a boolean if a field has been set.
func (o *TelemetryProviderConfig) HasIndex() bool {
	if o != nil && o.Index != nil {
		return true
	}

	return false
}

// SetIndex gets a reference to the given string and assigns it to the Index field.
func (o *TelemetryProviderConfig) SetIndex(v string) {
	o.Index = &v
}

// GetSource returns the Source field value if set, zero value otherwise.
func (o *TelemetryProviderConfig) GetSource() string {
	if o == nil || o.Source == nil {
		var ret string
		return ret
	}
	return *o.Source
}

// GetSourceOk returns a tuple with the Source field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *TelemetryProviderConfig) GetSourceOk() (*string, bool) {
	if o == nil || o.Source == nil {
		return nil, false
	}
	return o.Source, true
}

// HasSource returns a boolean if a field has been set.
func (o *TelemetryProviderConfig) HasSource() bool {
	if o != nil && o.Source != nil {
		return true
	}

	return false
}

// SetSource gets a reference to the given string and assigns it to the Source field.
func (o *TelemetryProviderConfig) SetSource(v string) {
	o.Source = &v
}

// GetSourceType returns the SourceType field value if set, zero value otherwise.
func (o *TelemetryProviderConfig) GetSourceType() string {
	if o == nil || o.SourceType == nil {
		var ret string
		return ret
	}
	return *o.SourceType
}

// GetSourceTypeOk returns a tuple with the SourceType field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *TelemetryProviderConfig) GetSourceTypeOk() (*string, bool) {
	if o == nil || o.SourceType == nil {
		return nil, false
	}
	return o.SourceType, true
}

// HasSourceType returns a boolean if a field has been set.
func (o *TelemetryProviderConfig) HasSourceType() bool {
	if o != nil && o.SourceType != nil {
		return true
	}

	return false
}

// SetSourceType gets a reference to the given string and assigns it to the SourceType field.
func (o *TelemetryProviderConfig) SetSourceType(v string) {
	o.SourceType = &v
}

// GetToken returns the Token field value if set, zero value otherwise.
func (o *TelemetryProviderConfig) GetToken() string {
	if o == nil || o.Token == nil {
		var ret string
		return ret
	}
	return *o.Token
}

// GetTokenOk returns a tuple with the Token field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *TelemetryProviderConfig) GetTokenOk() (*string, bool) {
	if o == nil || o.Token == nil {
		return nil, false
	}
	return o.Token, true
}

// HasToken returns a boolean if a field has been set.
func (o *TelemetryProviderConfig) HasToken() bool {
	if o != nil && o.Token != nil {
		return true
	}

	return false
}

// SetToken gets a reference to the given string and assigns it to the Token field.
func (o *TelemetryProviderConfig) SetToken(v string) {
	o.Token = &v
}

// GetAuthType returns the AuthType field value
func (o *TelemetryProviderConfig) GetAuthType() string {
	if o == nil {
		var ret string
		return ret
	}

	return o.AuthType
}

// GetAuthTypeOk returns a tuple with the AuthType field value
// and a boolean to check if the value has been set.
func (o *TelemetryProviderConfig) GetAuthTypeOk() (*string, bool) {
	if o == nil {
		return nil, false
	}
	return &o.AuthType, true
}

// SetAuthType sets field value
func (o *TelemetryProviderConfig) SetAuthType(v string) {
	o.AuthType = v
}

// GetBasicAuth returns the BasicAuth field value if set, zero value otherwise.
func (o *TelemetryProviderConfig) GetBasicAuth() ybaclient.BasicAuthCredentials {
	if o == nil || o.BasicAuth == nil {
		var ret ybaclient.BasicAuthCredentials
		return ret
	}
	return *o.BasicAuth
}

// GetBasicAuthOk returns a tuple with the BasicAuth field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *TelemetryProviderConfig) GetBasicAuthOk() (*ybaclient.BasicAuthCredentials, bool) {
	if o == nil || o.BasicAuth == nil {
		return nil, false
	}
	return o.BasicAuth, true
}

// HasBasicAuth returns a boolean if a field has been set.
func (o *TelemetryProviderConfig) HasBasicAuth() bool {
	if o != nil && o.BasicAuth != nil {
		return true
	}

	return false
}

// SetBasicAuth gets a reference to the given BasicAuthCredentials and assigns it to the BasicAuth field.
func (o *TelemetryProviderConfig) SetBasicAuth(v ybaclient.BasicAuthCredentials) {
	o.BasicAuth = &v
}

// GetOrganizationID returns the OrganizationID field value if set, zero value otherwise.
func (o *TelemetryProviderConfig) GetOrganizationID() string {
	if o == nil || o.OrganizationID == nil {
		var ret string
		return ret
	}
	return *o.OrganizationID
}

// GetOrganizationIDOk returns a tuple with the OrganizationID field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *TelemetryProviderConfig) GetOrganizationIDOk() (*string, bool) {
	if o == nil || o.OrganizationID == nil {
		return nil, false
	}
	return o.OrganizationID, true
}

// HasOrganizationID returns a boolean if a field has been set.
func (o *TelemetryProviderConfig) HasOrganizationID() bool {
	if o != nil && o.OrganizationID != nil {
		return true
	}

	return false
}

// SetOrganizationID gets a reference to the given string and assigns it to the OrganizationID field.
func (o *TelemetryProviderConfig) SetOrganizationID(v string) {
	o.OrganizationID = &v
}

// MarshalJSON serializes the struct using a buffer.
func (o TelemetryProviderConfig) MarshalJSON() ([]byte, error) {
	toSerialize := map[string]interface{}{}
	if o.Type != nil {
		toSerialize["type"] = o.Type
	}
	if o.AccessKey != "" {
		toSerialize["accessKey"] = o.AccessKey
	}
	if o.Endpoint != nil {
		toSerialize["endpoint"] = o.Endpoint
	}
	if o.LogGroup != "" {
		toSerialize["logGroup"] = o.LogGroup
	}
	if o.LogStream != "" {
		toSerialize["logStream"] = o.LogStream
	}
	if o.Region != "" {
		toSerialize["region"] = o.Region
	}
	if o.RoleARN != nil {
		toSerialize["roleARN"] = o.RoleARN
	}
	if o.SecretKey != "" {
		toSerialize["secretKey"] = o.SecretKey
	}
	if o.Project != nil {
		toSerialize["project"] = o.Project
	}
	// if o.Credentials != nil {
	// 	toSerialize["credentials"] = o.Credentials
	// }
	if o.ApiKey != nil {
		toSerialize["apiKey"] = o.ApiKey
	}
	if o.Site != nil {
		toSerialize["site"] = o.Site
	}
	if o.Index != nil {
		toSerialize["index"] = o.Index
	}
	if o.Source != nil {
		toSerialize["source"] = o.Source
	}
	if o.SourceType != nil {
		toSerialize["sourceType"] = o.SourceType
	}
	if o.Token != nil {
		toSerialize["token"] = o.Token
	}
	if true {
		toSerialize["authType"] = o.AuthType
	}
	if o.BasicAuth != nil {
		toSerialize["basicAuth"] = o.BasicAuth
	}
	if o.Endpoint != nil {
		toSerialize["endpoint"] = o.Endpoint
	}
	if o.OrganizationID != nil {
		toSerialize["organizationID"] = o.OrganizationID
	}

	return json.Marshal(toSerialize)
}

// NullableTelemetryProviderConfig is a helper abstraction for
// handling nullable telemetryproviderconfig types.
type NullableTelemetryProviderConfig struct {
	value *TelemetryProviderConfig
	isSet bool
}

// Get returns the value if set, or zero value otherwise
func (v NullableTelemetryProviderConfig) Get() *TelemetryProviderConfig {
	return v.value
}

// Set modifies the value
func (v *NullableTelemetryProviderConfig) Set(val *TelemetryProviderConfig) {
	v.value = val
	v.isSet = true
}

// IsSet returns true if the value is set
func (v NullableTelemetryProviderConfig) IsSet() bool {
	return v.isSet
}

// Unset removes the value
func (v *NullableTelemetryProviderConfig) Unset() {
	v.value = nil
	v.isSet = false
}

// NewNullableTelemetryProviderConfig creates a new NullableTelemetryProviderConfig
func NewNullableTelemetryProviderConfig(
	val *TelemetryProviderConfig,
) *NullableTelemetryProviderConfig {
	return &NullableTelemetryProviderConfig{value: val, isSet: true}
}

// MarshalJSON serializes the struct
func (v NullableTelemetryProviderConfig) MarshalJSON() ([]byte, error) {
	return json.Marshal(v.value)
}

// UnmarshalJSON implements the Unmarshaler interface.
func (v *NullableTelemetryProviderConfig) UnmarshalJSON(src []byte) error {
	v.isSet = true
	return json.Unmarshal(src, &v.value)
}
