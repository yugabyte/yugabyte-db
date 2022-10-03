package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// UpdateGflagMaintenance - API to set use_custom_gflags flag for a cluster's maintenance schedule
func (c *Container) UpdateGflagMaintenance(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
