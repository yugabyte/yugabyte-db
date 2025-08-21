/*
 * Copyright (c) YugaByte, Inc.
 */

package util

import (
	"encoding/json"
	"time"

	ybaclient "github.com/yugabyte/platform-go-client"
)

// CustomTime struct for marshaling/unmarshaling time.Time
type CustomTime struct {
	time.Time
}

// SchedulePagedResponse type
type SchedulePagedResponse struct {
	Entities   []Schedule `json:"entities"`
	HasNext    bool       `json:"hasNext"`
	HasPrev    bool       `json:"hasPrev"`
	TotalCount int32      `json:"totalCount"`
}

// Schedule type
type Schedule struct {
	// Backlog status of schedule arose due to conflicts
	BacklogStatus    *bool                      `json:"backlogStatus,omitempty"`
	CommonBackupInfo ybaclient.CommonBackupInfo `json:"backupInfo"`
	// Cron expression for the schedule
	CronExpression *string `json:"cronExpression,omitempty"`
	// Number of failed backup attempts
	FailureCount *int32 `json:"failureCount,omitempty"`
	// Frequency of the schedule, in milli seconds
	Frequency *int64 `json:"frequency,omitempty"`
	// Time unit of frequency
	FrequencyTimeUnit *string `json:"frequencyTimeUnit,omitempty"`
	// Backlog status of schedule of incremental backups arose due to conflicts
	IncrementBacklogStatus *bool `json:"incrementBacklogStatus,omitempty"`
	// Time on which schedule is expected to run for incremental backups
	NextIncrementScheduleTaskTime *CustomTime `json:"nextIncrementScheduleTaskTime,omitempty"`
	// Time on which schedule is expected to run
	NextScheduleTaskTime *CustomTime `json:"nextExpectedTask,omitempty"`
	// Owner UUID for the schedule
	OwnerUUID *string `json:"ownerUUID,omitempty"`
	// Running state of the schedule
	RunningState *bool `json:"runningState,omitempty"`
	// Name of the schedule
	ScheduleName *string `json:"scheduleName,omitempty"`
	// Schedule UUID
	ScheduleUUID *string `json:"scheduleUUID,omitempty"`
	// Status of the task. Possible values are _Active_, _Paused_, or _Stopped_.
	Status *string `json:"status,omitempty"`
	// Type of task to be scheduled.
	TaskType *string `json:"taskType,omitempty"`
	// Whether to use local timezone with cron expression for the schedule
	UseLocalTimezone *bool `json:"useLocalTimezone,omitempty"`
	// User who created the schedule policy
	UserEmail *string `json:"userEmail,omitempty"`
}

// GetEntities returns the Entities field value
func (o *SchedulePagedResponse) GetEntities() []Schedule {
	if o == nil {
		var ret []Schedule
		return ret
	}

	return o.Entities
}

// GetEntitiesOk returns a tuple with the Entities field value
// and a boolean to check if the value has been set.
func (o *SchedulePagedResponse) GetEntitiesOk() (*[]Schedule, bool) {
	if o == nil {
		return nil, false
	}
	return &o.Entities, true
}

// SetEntities sets field value
func (o *SchedulePagedResponse) SetEntities(v []Schedule) {
	o.Entities = v
}

// GetHasNext returns the HasNext field value
func (o *SchedulePagedResponse) GetHasNext() bool {
	if o == nil {
		var ret bool
		return ret
	}

	return o.HasNext
}

// GetHasNextOk returns a tuple with the HasNext field value
// and a boolean to check if the value has been set.
func (o *SchedulePagedResponse) GetHasNextOk() (*bool, bool) {
	if o == nil {
		return nil, false
	}
	return &o.HasNext, true
}

// SetHasNext sets field value
func (o *SchedulePagedResponse) SetHasNext(v bool) {
	o.HasNext = v
}

// GetHasPrev returns the HasPrev field value
func (o *SchedulePagedResponse) GetHasPrev() bool {
	if o == nil {
		var ret bool
		return ret
	}

	return o.HasPrev
}

// GetHasPrevOk returns a tuple with the HasPrev field value
// and a boolean to check if the value has been set.
func (o *SchedulePagedResponse) GetHasPrevOk() (*bool, bool) {
	if o == nil {
		return nil, false
	}
	return &o.HasPrev, true
}

// SetHasPrev sets field value
func (o *SchedulePagedResponse) SetHasPrev(v bool) {
	o.HasPrev = v
}

// GetTotalCount returns the TotalCount field value
func (o *SchedulePagedResponse) GetTotalCount() int32 {
	if o == nil {
		var ret int32
		return ret
	}

	return o.TotalCount
}

// GetTotalCountOk returns a tuple with the TotalCount field value
// and a boolean to check if the value has been set.
func (o *SchedulePagedResponse) GetTotalCountOk() (*int32, bool) {
	if o == nil {
		return nil, false
	}
	return &o.TotalCount, true
}

// SetTotalCount sets field value
func (o *SchedulePagedResponse) SetTotalCount(v int32) {
	o.TotalCount = v
}

// MarshalJSON returns the JSON representation for the model.
func (o SchedulePagedResponse) MarshalJSON() ([]byte, error) {
	toSerialize := map[string]interface{}{}
	if true {
		toSerialize["entities"] = o.Entities
	}
	if true {
		toSerialize["hasNext"] = o.HasNext
	}
	if true {
		toSerialize["hasPrev"] = o.HasPrev
	}
	if true {
		toSerialize["totalCount"] = o.TotalCount
	}
	return json.Marshal(toSerialize)
}

// NullableSchedulePagedResponse handles when a null is used
type NullableSchedulePagedResponse struct {
	value *SchedulePagedResponse
	isSet bool
}

// Get returns the value if set, or zero value otherwise
func (v NullableSchedulePagedResponse) Get() *SchedulePagedResponse {
	return v.value
}

// Set modifies the value
func (v *NullableSchedulePagedResponse) Set(val *SchedulePagedResponse) {
	v.value = val
	v.isSet = true
}

// IsSet returns true if Set has been called
func (v NullableSchedulePagedResponse) IsSet() bool {
	return v.isSet
}

// Unset removes the value
func (v *NullableSchedulePagedResponse) Unset() {
	v.value = nil
	v.isSet = false
}

// NewNullableSchedulePagedResponse returns a pointer to a new
// instance of NullableSchedulePagedResponse
func NewNullableSchedulePagedResponse(val *SchedulePagedResponse) *NullableSchedulePagedResponse {
	return &NullableSchedulePagedResponse{value: val, isSet: true}
}

// MarshalJSON returns the JSON representation for the model.
func (v NullableSchedulePagedResponse) MarshalJSON() ([]byte, error) {
	return json.Marshal(v.value)
}

// UnmarshalJSON implements the Unmarshaler interface.
func (v *NullableSchedulePagedResponse) UnmarshalJSON(src []byte) error {
	v.isSet = true
	return json.Unmarshal(src, &v.value)
}

// GetBacklogStatus returns the BacklogStatus field value if set, zero value otherwise.
func (o *Schedule) GetBacklogStatus() bool {
	if o == nil || o.BacklogStatus == nil {
		var ret bool
		return ret
	}
	return *o.BacklogStatus
}

// GetBacklogStatusOk returns a tuple with the BacklogStatus field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *Schedule) GetBacklogStatusOk() (*bool, bool) {
	if o == nil || o.BacklogStatus == nil {
		return nil, false
	}
	return o.BacklogStatus, true
}

// HasBacklogStatus returns a boolean if a field has been set.
func (o *Schedule) HasBacklogStatus() bool {
	if o != nil && o.BacklogStatus != nil {
		return true
	}

	return false
}

// SetBacklogStatus gets a reference to the given bool and assigns it to the BacklogStatus field.
func (o *Schedule) SetBacklogStatus(v bool) {
	o.BacklogStatus = &v
}

// GetCommonBackupInfo returns the CommonBackupInfo field value
func (o *Schedule) GetCommonBackupInfo() ybaclient.CommonBackupInfo {
	if o == nil {
		var ret ybaclient.CommonBackupInfo
		return ret
	}

	return o.CommonBackupInfo
}

// GetCommonBackupInfoOk returns a tuple with the CommonBackupInfo field value
// and a boolean to check if the value has been set.
func (o *Schedule) GetCommonBackupInfoOk() (*ybaclient.CommonBackupInfo, bool) {
	if o == nil {
		return nil, false
	}
	return &o.CommonBackupInfo, true
}

// SetCommonBackupInfo sets field value
func (o *Schedule) SetCommonBackupInfo(v ybaclient.CommonBackupInfo) {
	o.CommonBackupInfo = v
}

// GetCronExpression returns the CronExpression field value if set, zero value otherwise.
func (o *Schedule) GetCronExpression() string {
	if o == nil || o.CronExpression == nil {
		var ret string
		return ret
	}
	return *o.CronExpression
}

// GetCronExpressionOk returns a tuple with the CronExpression field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *Schedule) GetCronExpressionOk() (*string, bool) {
	if o == nil || o.CronExpression == nil {
		return nil, false
	}
	return o.CronExpression, true
}

// HasCronExpression returns a boolean if a field has been set.
func (o *Schedule) HasCronExpression() bool {
	if o != nil && o.CronExpression != nil {
		return true
	}

	return false
}

// SetCronExpression gets a reference to the given string
// and assigns it to the CronExpression field.
func (o *Schedule) SetCronExpression(v string) {
	o.CronExpression = &v
}

// GetFailureCount returns the FailureCount field value if set, zero value otherwise.
func (o *Schedule) GetFailureCount() int32 {
	if o == nil || o.FailureCount == nil {
		var ret int32
		return ret
	}
	return *o.FailureCount
}

// GetFailureCountOk returns a tuple with the FailureCount field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *Schedule) GetFailureCountOk() (*int32, bool) {
	if o == nil || o.FailureCount == nil {
		return nil, false
	}
	return o.FailureCount, true
}

// HasFailureCount returns a boolean if a field has been set.
func (o *Schedule) HasFailureCount() bool {
	if o != nil && o.FailureCount != nil {
		return true
	}

	return false
}

// SetFailureCount gets a reference to the given int32 and assigns it to the FailureCount field.
func (o *Schedule) SetFailureCount(v int32) {
	o.FailureCount = &v
}

// GetFrequency returns the Frequency field value if set, zero value otherwise.
func (o *Schedule) GetFrequency() int64 {
	if o == nil || o.Frequency == nil {
		var ret int64
		return ret
	}
	return *o.Frequency
}

// GetFrequencyOk returns a tuple with the Frequency field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *Schedule) GetFrequencyOk() (*int64, bool) {
	if o == nil || o.Frequency == nil {
		return nil, false
	}
	return o.Frequency, true
}

// HasFrequency returns a boolean if a field has been set.
func (o *Schedule) HasFrequency() bool {
	if o != nil && o.Frequency != nil {
		return true
	}

	return false
}

// SetFrequency gets a reference to the given int64 and assigns it to the Frequency field.
func (o *Schedule) SetFrequency(v int64) {
	o.Frequency = &v
}

// GetFrequencyTimeUnit returns the FrequencyTimeUnit field value if set, zero value otherwise.
func (o *Schedule) GetFrequencyTimeUnit() string {
	if o == nil || o.FrequencyTimeUnit == nil {
		var ret string
		return ret
	}
	return *o.FrequencyTimeUnit
}

// GetFrequencyTimeUnitOk returns a tuple with the FrequencyTimeUnit
// field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *Schedule) GetFrequencyTimeUnitOk() (*string, bool) {
	if o == nil || o.FrequencyTimeUnit == nil {
		return nil, false
	}
	return o.FrequencyTimeUnit, true
}

// HasFrequencyTimeUnit returns a boolean if a field has been set.
func (o *Schedule) HasFrequencyTimeUnit() bool {
	if o != nil && o.FrequencyTimeUnit != nil {
		return true
	}

	return false
}

// SetFrequencyTimeUnit gets a reference to the given string
// and assigns it to the FrequencyTimeUnit field.
func (o *Schedule) SetFrequencyTimeUnit(v string) {
	o.FrequencyTimeUnit = &v
}

// GetIncrementBacklogStatus returns the IncrementBacklogStatus
// field value if set, zero value otherwise.
func (o *Schedule) GetIncrementBacklogStatus() bool {
	if o == nil || o.IncrementBacklogStatus == nil {
		var ret bool
		return ret
	}
	return *o.IncrementBacklogStatus
}

// GetIncrementBacklogStatusOk returns a tuple with
// the IncrementBacklogStatus field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *Schedule) GetIncrementBacklogStatusOk() (*bool, bool) {
	if o == nil || o.IncrementBacklogStatus == nil {
		return nil, false
	}
	return o.IncrementBacklogStatus, true
}

// HasIncrementBacklogStatus returns a boolean if a field has been set.
func (o *Schedule) HasIncrementBacklogStatus() bool {
	if o != nil && o.IncrementBacklogStatus != nil {
		return true
	}

	return false
}

// SetIncrementBacklogStatus gets a reference to the
// given bool and assigns it to the IncrementBacklogStatus field.
func (o *Schedule) SetIncrementBacklogStatus(v bool) {
	o.IncrementBacklogStatus = &v
}

// GetNextIncrementScheduleTaskTime returns
// the NextIncrementScheduleTaskTime field value if set, zero value otherwise.
func (o *Schedule) GetNextIncrementScheduleTaskTime() CustomTime {
	if o == nil || o.NextIncrementScheduleTaskTime == nil {
		var ret CustomTime
		return ret
	}
	return *o.NextIncrementScheduleTaskTime
}

// GetNextIncrementScheduleTaskTimeOk returns
// a tuple with the NextIncrementScheduleTaskTime field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *Schedule) GetNextIncrementScheduleTaskTimeOk() (CustomTime, bool) {
	if o == nil || o.NextIncrementScheduleTaskTime == nil {
		return CustomTime{}, false
	}
	return *o.NextIncrementScheduleTaskTime, true
}

// HasNextIncrementScheduleTaskTime returns a boolean if a field has been set.
func (o *Schedule) HasNextIncrementScheduleTaskTime() bool {
	if o != nil && o.NextIncrementScheduleTaskTime != nil {
		return true
	}

	return false
}

// SetNextIncrementScheduleTaskTime gets a reference
// to the given time.Time and assigns it to the NextIncrementScheduleTaskTime field.
func (o *Schedule) SetNextIncrementScheduleTaskTime(v CustomTime) {
	o.NextIncrementScheduleTaskTime = &v
}

// GetNextScheduleTaskTime returns the NextScheduleTaskTime
// field value if set, zero value otherwise.
func (o *Schedule) GetNextScheduleTaskTime() CustomTime {
	if o == nil || o.NextScheduleTaskTime == nil {
		var ret CustomTime
		return ret
	}
	return *o.NextScheduleTaskTime
}

// GetNextScheduleTaskTimeOk returns a tuple with the
// NextScheduleTaskTime field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *Schedule) GetNextScheduleTaskTimeOk() (*CustomTime, bool) {
	if o == nil || o.NextScheduleTaskTime == nil {
		return nil, false
	}
	return o.NextScheduleTaskTime, true
}

// HasNextScheduleTaskTime returns a boolean if a field has been set.
func (o *Schedule) HasNextScheduleTaskTime() bool {
	if o != nil && o.NextScheduleTaskTime != nil {
		return true
	}

	return false
}

// SetNextScheduleTaskTime gets a reference
// to the given time.Time and assigns it to the NextScheduleTaskTime field.
func (o *Schedule) SetNextScheduleTaskTime(v CustomTime) {
	o.NextScheduleTaskTime = &v
}

// GetOwnerUUID returns the OwnerUUID field value if set, zero value otherwise.
func (o *Schedule) GetOwnerUUID() string {
	if o == nil || o.OwnerUUID == nil {
		var ret string
		return ret
	}
	return *o.OwnerUUID
}

// GetOwnerUUIDOk returns a tuple with the OwnerUUID field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *Schedule) GetOwnerUUIDOk() (*string, bool) {
	if o == nil || o.OwnerUUID == nil {
		return nil, false
	}
	return o.OwnerUUID, true
}

// HasOwnerUUID returns a boolean if a field has been set.
func (o *Schedule) HasOwnerUUID() bool {
	if o != nil && o.OwnerUUID != nil {
		return true
	}

	return false
}

// SetOwnerUUID gets a reference to the given string and assigns it to the OwnerUUID field.
func (o *Schedule) SetOwnerUUID(v string) {
	o.OwnerUUID = &v
}

// GetRunningState returns the RunningState field value if set, zero value otherwise.
func (o *Schedule) GetRunningState() bool {
	if o == nil || o.RunningState == nil {
		var ret bool
		return ret
	}
	return *o.RunningState
}

// GetRunningStateOk returns a tuple with the RunningState field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *Schedule) GetRunningStateOk() (*bool, bool) {
	if o == nil || o.RunningState == nil {
		return nil, false
	}
	return o.RunningState, true
}

// HasRunningState returns a boolean if a field has been set.
func (o *Schedule) HasRunningState() bool {
	if o != nil && o.RunningState != nil {
		return true
	}

	return false
}

// SetRunningState gets a reference to the given bool and assigns it to the RunningState field.
func (o *Schedule) SetRunningState(v bool) {
	o.RunningState = &v
}

// GetScheduleName returns the ScheduleName field value if set, zero value otherwise.
func (o *Schedule) GetScheduleName() string {
	if o == nil || o.ScheduleName == nil {
		var ret string
		return ret
	}
	return *o.ScheduleName
}

// GetScheduleNameOk returns a tuple with the ScheduleName field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *Schedule) GetScheduleNameOk() (*string, bool) {
	if o == nil || o.ScheduleName == nil {
		return nil, false
	}
	return o.ScheduleName, true
}

// HasScheduleName returns a boolean if a field has been set.
func (o *Schedule) HasScheduleName() bool {
	if o != nil && o.ScheduleName != nil {
		return true
	}

	return false
}

// SetScheduleName gets a reference to the given string and assigns it to the ScheduleName field.
func (o *Schedule) SetScheduleName(v string) {
	o.ScheduleName = &v
}

// GetScheduleUUID returns the ScheduleUUID field value if set, zero value otherwise.
func (o *Schedule) GetScheduleUUID() string {
	if o == nil || o.ScheduleUUID == nil {
		var ret string
		return ret
	}
	return *o.ScheduleUUID
}

// GetScheduleUUIDOk returns a tuple with the ScheduleUUID field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *Schedule) GetScheduleUUIDOk() (*string, bool) {
	if o == nil || o.ScheduleUUID == nil {
		return nil, false
	}
	return o.ScheduleUUID, true
}

// HasScheduleUUID returns a boolean if a field has been set.
func (o *Schedule) HasScheduleUUID() bool {
	if o != nil && o.ScheduleUUID != nil {
		return true
	}

	return false
}

// SetScheduleUUID gets a reference to the given string and assigns it to the ScheduleUUID field.
func (o *Schedule) SetScheduleUUID(v string) {
	o.ScheduleUUID = &v
}

// GetStatus returns the Status field value if set, zero value otherwise.
func (o *Schedule) GetStatus() string {
	if o == nil || o.Status == nil {
		var ret string
		return ret
	}
	return *o.Status
}

// GetStatusOk returns a tuple with the Status field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *Schedule) GetStatusOk() (*string, bool) {
	if o == nil || o.Status == nil {
		return nil, false
	}
	return o.Status, true
}

// HasStatus returns a boolean if a field has been set.
func (o *Schedule) HasStatus() bool {
	if o != nil && o.Status != nil {
		return true
	}

	return false
}

// SetStatus gets a reference to the given string and assigns it to the Status field.
func (o *Schedule) SetStatus(v string) {
	o.Status = &v
}

// GetTaskType returns the TaskType field value if set, zero value otherwise.
func (o *Schedule) GetTaskType() string {
	if o == nil || o.TaskType == nil {
		var ret string
		return ret
	}
	return *o.TaskType
}

// GetTaskTypeOk returns a tuple with the TaskType field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *Schedule) GetTaskTypeOk() (*string, bool) {
	if o == nil || o.TaskType == nil {
		return nil, false
	}
	return o.TaskType, true
}

// HasTaskType returns a boolean if a field has been set.
func (o *Schedule) HasTaskType() bool {
	if o != nil && o.TaskType != nil {
		return true
	}

	return false
}

// SetTaskType gets a reference to the given string and assigns it to the TaskType field.
func (o *Schedule) SetTaskType(v string) {
	o.TaskType = &v
}

// GetUseLocalTimezone returns the UseLocalTimezone field value if set, zero value otherwise.
func (o *Schedule) GetUseLocalTimezone() bool {
	if o == nil || o.UseLocalTimezone == nil {
		var ret bool
		return ret
	}
	return *o.UseLocalTimezone
}

// GetUseLocalTimezoneOk returns a tuple with the UseLocalTimezone field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *Schedule) GetUseLocalTimezoneOk() (*bool, bool) {
	if o == nil || o.UseLocalTimezone == nil {
		return nil, false
	}
	return o.UseLocalTimezone, true
}

// HasUseLocalTimezone returns a boolean if a field has been set.
func (o *Schedule) HasUseLocalTimezone() bool {
	if o != nil && o.UseLocalTimezone != nil {
		return true
	}

	return false
}

// SetUseLocalTimezone gets a reference to the given bool
// and assigns it to the UseLocalTimezone field.
func (o *Schedule) SetUseLocalTimezone(v bool) {
	o.UseLocalTimezone = &v
}

// GetUserEmail returns the UserEmail field value if set, zero value otherwise.
func (o *Schedule) GetUserEmail() string {
	if o == nil || o.UserEmail == nil {
		var ret string
		return ret
	}
	return *o.UserEmail
}

// GetUserEmailOk returns a tuple with the UserEmail field value if set, nil otherwise
// and a boolean to check if the value has been set.
func (o *Schedule) GetUserEmailOk() (*string, bool) {
	if o == nil || o.UserEmail == nil {
		return nil, false
	}
	return o.UserEmail, true
}

// HasUserEmail returns a boolean if a field has been set.
func (o *Schedule) HasUserEmail() bool {
	if o != nil && o.UserEmail != nil {
		return true
	}

	return false
}

// SetUserEmail gets a reference to the given string and assigns it to the UserEmail field.
func (o *Schedule) SetUserEmail(v string) {
	o.UserEmail = &v
}

// MarshalJSON implements the json.Marshaler interface.
func (o Schedule) MarshalJSON() ([]byte, error) {
	toSerialize := map[string]interface{}{}
	if o.BacklogStatus != nil {
		toSerialize["backlogStatus"] = o.BacklogStatus
	}
	if true {
		toSerialize["backupInfo"] = o.CommonBackupInfo
	}
	if o.CronExpression != nil {
		toSerialize["cronExpression"] = o.CronExpression
	}
	if o.FailureCount != nil {
		toSerialize["failureCount"] = o.FailureCount
	}
	if o.Frequency != nil {
		toSerialize["frequency"] = o.Frequency
	}
	if o.FrequencyTimeUnit != nil {
		toSerialize["frequencyTimeUnit"] = o.FrequencyTimeUnit
	}
	if o.IncrementBacklogStatus != nil {
		toSerialize["incrementBacklogStatus"] = o.IncrementBacklogStatus
	}
	if o.NextIncrementScheduleTaskTime != nil {
		toSerialize["nextIncrementScheduleTaskTime"] = o.NextIncrementScheduleTaskTime
	}
	if o.NextScheduleTaskTime != nil {
		toSerialize["nextExpectedTask"] = o.NextScheduleTaskTime
	}
	if o.OwnerUUID != nil {
		toSerialize["ownerUUID"] = o.OwnerUUID
	}
	if o.RunningState != nil {
		toSerialize["runningState"] = o.RunningState
	}
	if o.ScheduleName != nil {
		toSerialize["scheduleName"] = o.ScheduleName
	}
	if o.ScheduleUUID != nil {
		toSerialize["scheduleUUID"] = o.ScheduleUUID
	}
	if o.Status != nil {
		toSerialize["status"] = o.Status
	}
	if o.TaskType != nil {
		toSerialize["taskType"] = o.TaskType
	}
	if o.UseLocalTimezone != nil {
		toSerialize["useLocalTimezone"] = o.UseLocalTimezone
	}
	if o.UserEmail != nil {
		toSerialize["userEmail"] = o.UserEmail
	}
	return json.Marshal(toSerialize)
}

// NullableSchedule handles when a null is used for Schedule
type NullableSchedule struct {
	value *Schedule
	isSet bool
}

// Get handles when a null is used for Schedule
func (v NullableSchedule) Get() *Schedule {
	return v.value
}

// Set handles when a null is used for Schedule
func (v *NullableSchedule) Set(val *Schedule) {
	v.value = val
	v.isSet = true
}

// IsSet returns true if the Schedule property is set
func (v NullableSchedule) IsSet() bool {
	return v.isSet
}

// Unset removes the Schedule property
func (v *NullableSchedule) Unset() {
	v.value = nil
	v.isSet = false
}

// NewNullableSchedule handles when a null is used for Schedule
func NewNullableSchedule(val *Schedule) *NullableSchedule {
	return &NullableSchedule{value: val, isSet: true}
}

// MarshalJSON implements the json.Marshaler interface.
func (v NullableSchedule) MarshalJSON() ([]byte, error) {
	return json.Marshal(v.value)
}

// UnmarshalJSON implements the Unmarshaler interface.
func (v *NullableSchedule) UnmarshalJSON(src []byte) error {
	v.isSet = true
	return json.Unmarshal(src, &v.value)
}

// UnmarshalJSON implements the Unmarshaler interface.
func (ct *CustomTime) UnmarshalJSON(b []byte) error {
	var timestamp int64
	if err := json.Unmarshal(b, &timestamp); err != nil {
		return err
	}

	// Convert from milliseconds to seconds for Unix timestamp
	ct.Time = time.UnixMilli(timestamp)
	return nil
}
