package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// GetLoggingLevel - Get Logging Level
func (c *Container) GetLoggingLevel(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// SetLoggingLevel - Set Logging Level
func (c *Container) SetLoggingLevel(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
