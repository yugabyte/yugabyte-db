package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// DeleteAdminApiToken - Delete admin token
func (c *Container) DeleteAdminApiToken(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetAdminApiToken - Create an admin JWT for bearer authentication
func (c *Container) GetAdminApiToken(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListAdminApiTokens - List admin JWTs
func (c *Container) ListAdminApiTokens(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
