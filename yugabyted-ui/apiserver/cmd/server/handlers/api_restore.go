package handlers

import (
    "apiserver/cmd/server/helpers"
    "apiserver/cmd/server/models"
    "fmt"
    "net/http"
    "github.com/labstack/echo/v4"
)

func (c *Container) GetRestoreDetails(ctx echo.Context) error {
    var operationType = "restore"
    lockfilelistFuture := make(chan helpers.LockFileListFuture, 1)
    c.helper.ListLockFilesFuture(helpers.DataDir, operationType, lockfilelistFuture)
    lockfilelistResult := <-lockfilelistFuture
    if lockfilelistResult.Error != nil {
        return ctx.String(http.StatusInternalServerError,
                        fmt.Sprintf("Failed to list lock files: %v", lockfilelistResult.Error))
    }
    if len(lockfilelistResult.Result) == 0 {
        return ctx.JSON(http.StatusOK, []interface{}{})
    }
    var lockFileDetails helpers.LockFileDetails
    lockFileInfoFutures := make([]chan helpers.LockFileInfoFuture,
                                                        len(lockfilelistResult.Result))
    taskProgressInfoFutures := make([]chan helpers.TaskProgressInfoFuture,
                                                        len(lockfilelistResult.Result))

    for i, lockFilePath := range lockfilelistResult.Result {
        lockFileInfoFutures[i] = make(chan helpers.LockFileInfoFuture)
        // Fetch lock file info asynchronously
        go c.helper.GetLockFileInfoFuture(lockFilePath, lockFileInfoFutures[i])
    }
    // Asynchronously start task progress
    for i := range lockfilelistResult.Result {
        lockFileInfo := <-lockFileInfoFutures[i]
        if lockFileInfo.Error != nil {
            return ctx.String(http.StatusInternalServerError,
                    fmt.Sprintf("Failed to parse lock files: %v", lockFileInfo.Error))
        }
        c.logger.Infof("Successfully read lock file for database/keyspace: %s",
                                                       lockFileInfo.DatabaseKeyspace)
        lockFileDetails.Data = append(lockFileDetails.Data, lockFileInfo)
        taskProgressInfoFutures[i] = make(chan helpers.TaskProgressInfoFuture)
        go c.helper.GetYBCTaskProgress(lockFileInfo.YbcTaskID, lockFileInfo.TserverIP,
                                                                taskProgressInfoFutures[i])
    }

    var response models.RestoreResponse
    for i, lockFileInfo := range lockFileDetails.Data {
        taskProgressInfo := <-taskProgressInfoFutures[i]
        if taskProgressInfo.Error != nil {
            return ctx.String(http.StatusInternalServerError,
                fmt.Sprintf("Failed to get task progress: %v", taskProgressInfo.Error))
        }
        response.Restore = append(response.Restore, models.RestoreDetails{
            YbcTaskId:        lockFileInfo.YbcTaskID,
            TserverIp:        lockFileInfo.TserverIP,
            UserOperation:    lockFileInfo.UserOperation,
            YbdbApi:          lockFileInfo.YbdbAPI,
            DatabaseKeyspace: lockFileInfo.DatabaseKeyspace,
            TaskStartTime:    lockFileInfo.TaskStartTime,
            TaskStatus:       taskProgressInfo.FinalStatus,
            TimeTaken:        taskProgressInfo.TimeTaken,
            BytesTransferred: taskProgressInfo.BytesTransferred,
            ActualSize:       taskProgressInfo.ActualSize,
        })
    }
    return ctx.JSON(http.StatusOK, response)
}
