package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// CreateBackup - Create backups
func (c *Container) CreateBackup(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// DeleteBackup - Delete a backup
func (c *Container) DeleteBackup(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// DeleteClusterBackups - Submit task to delete all backups of a cluster
func (c *Container) DeleteClusterBackups(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// DeleteSchedule - Delete a schedule
func (c *Container) DeleteSchedule(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// EditBackupSchedule - Edit the backup schedule
func (c *Container) EditBackupSchedule(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetBackup - Get a backup
func (c *Container) GetBackup(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetSchedule - Get a schedule
func (c *Container) GetSchedule(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListBackups - List backups
func (c *Container) ListBackups(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListRestores - List restore operations
func (c *Container) ListRestores(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListSchedules - List schedules
func (c *Container) ListSchedules(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// RestoreBackup - Restore a backup to a Cluster
func (c *Container) RestoreBackup(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ScheduleBackup - Schedule a backup
func (c *Container) ScheduleBackup(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
