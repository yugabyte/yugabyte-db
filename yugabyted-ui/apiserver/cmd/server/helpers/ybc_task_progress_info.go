package helpers

import (
    "strings"
    "fmt"
)

type BackupTaskResponse struct {
    YbcTaskID        string `json:"ybc_task_id"`
    TserverIP        string `json:"tserver_ip"`
    UserOperation    string `json:"user_operation"`
    YbdbAPI          string `json:"ybdb_api"`
    DatabaseKeyspace string `json:"database_keyspace"`
    TaskStartTime    string `json:"task_start_time"`
    TaskStatus       string `json:"task_status"`
    TimeTaken        string `json:"time_taken"`
    BytesTransferred string `json:"bytes_transferred"`
    ActualSize       string `json:"actual_size"`
}

type TaskProgressInfoFuture struct {
    BytesTransferred     string
    ActualSize           string
    TimeTaken            string
    FinalStatus          string
    Error                error
}

type TaskProgressList struct {
    Data  []TaskProgressInfoFuture
}

func (h *HelperContainer) GetYBCTaskProgress(taskID string, tserverIP string,
                                            future chan TaskProgressInfoFuture) {
    var taskProgressInfo TaskProgressInfoFuture
    h.logger.Infof(
        "Attempting to run task progress for TaskID: %s, TServerIP: %s. ",
                                                             taskID, tserverIP)
    ybControllerResult, ybControllerError := h.TaskProgress(taskID, tserverIP)
    if ybControllerError != nil {
        taskProgressInfo.Error = fmt.Errorf(
            "failed to get YBC task progress for taskID %s and tserverIP %s: %v",
                                            taskID, tserverIP, ybControllerError)
        future <- taskProgressInfo
        return
    }
    h.logger.Infof(
        "Successfully retrieved task progress for TaskID: %s, TServerIP: %s. ",
                                                             taskID, tserverIP)
    ybControllerResult = strings.ReplaceAll(ybControllerResult, "\r", "\n")
    lines := strings.Split(ybControllerResult, "\n")
    for _, line := range lines {
        parts := strings.SplitN(line, ": ", 2)
        if len(parts) < 2 {
            continue
        }
        key := strings.TrimSpace(parts[0])
        value := strings.TrimSpace(parts[1])
        switch key {
        case "Bytes Transferred":
            taskProgressInfo.BytesTransferred = value
        case "Actual Size":
            taskProgressInfo.ActualSize = value
        case "Time Taken":
            taskProgressInfo.TimeTaken = value
        case "Final Status":
            taskProgressInfo.FinalStatus = value
        }
    }
    future <- taskProgressInfo
}
