package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// DelayMaintenanceEvent - API to delay maintenance events for a cluster
func (c *Container) DelayMaintenanceEvent(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetMaintenanceSchedule - API to get maintenance schedules
func (c *Container) GetMaintenanceSchedule(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetNextMaintenanceWindowInfo - API to get next maintenance window for a cluster
func (c *Container) GetNextMaintenanceWindowInfo(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListScheduledMaintenanceEventsForCluster - API to list all scheduled maintenance events for a cluster
func (c *Container) ListScheduledMaintenanceEventsForCluster(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// TriggerMaintenanceEvent - API to trigger maintenance events for a cluster
func (c *Container) TriggerMaintenanceEvent(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// UpdateMaintenanceSchedule - API to update maintenance schedules
func (c *Container) UpdateMaintenanceSchedule(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
