package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// GetBackupInfo - Get backup info along with the location
func (c *Container) GetBackupInfo(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// RestoreMigrationBackup - Restore a backup from the specified bucket to a Cluster
func (c *Container) RestoreMigrationBackup(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
