package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// CancelScheduledUpgrade - Cancel a scheduled upgrade task
func (c *Container) CancelScheduledUpgrade(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListScheduledUpgrades - List currently scheduled upgrade tasks
func (c *Container) ListScheduledUpgrades(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// RemoveClusterFromExecution - Remove a cluster from a scheduled upgrade execution
func (c *Container) RemoveClusterFromExecution(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ScheduleBulkUpgrade - Schedule an upgrade based on cluster tier and optionally cloud/region
func (c *Container) ScheduleBulkUpgrade(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ScheduleClusterUpgrade - Schedule an Upgrade for the specified Cluster
func (c *Container) ScheduleClusterUpgrade(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
