package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// EventCallback - Post a task-related event callback
func (c *Container) EventCallback(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
