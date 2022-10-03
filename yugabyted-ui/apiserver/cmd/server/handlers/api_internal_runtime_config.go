package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// GetRuntimeConfig - Get runtime configuration
func (c *Container) GetRuntimeConfig(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// UpdateRuntimeConfig - Update configuration keys for given scope.
func (c *Container) UpdateRuntimeConfig(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
