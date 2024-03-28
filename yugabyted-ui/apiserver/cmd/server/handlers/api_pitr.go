package handlers

import (
    "apiserver/cmd/server/helpers"
    "apiserver/cmd/server/models"
    "fmt"
    "net/http"
    "strconv"
    "strings"
    "time"

    "github.com/labstack/echo/v4"
)

const (
    _24h_FORMAT = "2006-01-02 15:04:05"
    _12h_FORMAT = "2006-01-02 03:04:05 PM"
)

// Get PITR Configs
func (c *Container) GetPITRConfigurations(ctx echo.Context) error {
    pitrConfigFuture := make(chan helpers.PITRConfigFuture)
    go c.helper.GetPITRConfigFuture(pitrConfigFuture)
    pitrConfigResult := <-pitrConfigFuture
    if pitrConfigResult.Error != nil {
        errorMessage := fmt.Sprintf("Error retrieving PITR configuration: %v",
                                                         pitrConfigResult.Error)
        return ctx.String(http.StatusInternalServerError, errorMessage)
    }
    c.logger.Infof("Successfully fetched and parsed PITR configuration")
    var response models.PITRScheduleInfo
    activeSchedules := false
    for i := len(pitrConfigResult.Config.Schedules) - 1; i >= 0; i-- {
        schedule := pitrConfigResult.Config.Schedules[i]
        if schedule.Options.DeleteTime == "" {
            activeSchedules = true
            break
        }
    }
    if activeSchedules {
        var schedule_entry int32 = 0
        for _, schedule := range pitrConfigResult.Config.Schedules {
            if schedule.Options.DeleteTime != "" {
                continue
            }
            schedule_entry++
            filter := schedule.Options.Filter
            name := strings.Split(filter, ".")[1]
            interval := "24 hours"
            retentionInMinutes,_ := strconv.Atoi(strings.Split(schedule.Options.Retention, " ")[0])
            retentionInDays := retentionInMinutes / (24 * 60)
            retention := fmt.Sprintf("%d days", retentionInDays)
            earliest_recoverable_time := schedule.Snapshots[0].SnapshotTime
            parsedTime, err := time.Parse(_24h_FORMAT, earliest_recoverable_time)
            if err != nil {
                return ctx.JSON(http.StatusInternalServerError, map[string]string{
                    "error": "Failed to parse snapshot time: " + err.Error(),
                })
            }
            modifiedTime := parsedTime.Add(time.Second).Truncate(time.Second)
            formattedTime := modifiedTime.Format(_12h_FORMAT)

            scheduleEntry := models.PitrSchedule{
                Id:                           schedule_entry,
                DatabaseKeyspace:             name,
                Interval:                     interval,
                Retention:                    retention,
                EarliestRecoverableTime:      formattedTime,
                }
            response.Schedules = append(response.Schedules, scheduleEntry)
        }
    } else {
        return ctx.JSON(http.StatusOK, []interface{}{})
    }
    return ctx.JSON(http.StatusOK, response)
}
