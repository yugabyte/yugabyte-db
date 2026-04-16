package models

// PitrSchedule - Details of a Point-in-Time Recovery (PITR) schedule
type PitrSchedule struct {

    Id int32 `json:"id"`

    DatabaseKeyspace string `json:"databaseKeyspace"`

    Interval string `json:"interval"`

    Retention string `json:"retention"`

    EarliestRecoverableTime string `json:"earliestRecoverableTime"`
}
