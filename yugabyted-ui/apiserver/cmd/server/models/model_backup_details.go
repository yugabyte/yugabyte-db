package models

// BackupDetails - Details of backup enabled on databases in YugabyteDB cluster
type BackupDetails struct {

    YbcTaskId string `json:"ybc_task_id"`

    TserverIp string `json:"tserver_ip"`

    UserOperation string `json:"user_operation"`

    YbdbApi string `json:"ybdb_api"`

    DatabaseKeyspace string `json:"database_keyspace"`

    TaskStartTime string `json:"task_start_time"`

    TaskStatus string `json:"task_status"`

    TimeTaken string `json:"time_taken"`

    BytesTransferred string `json:"bytes_transferred"`

    ActualSize string `json:"actual_size"`
}
