package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// CreateAuthToken - Create a new auth token
func (c *Container) CreateAuthToken(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// DeleteAuthToken - Delete auth token
func (c *Container) DeleteAuthToken(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetAuthToken - Get auth token
func (c *Container) GetAuthToken(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListAuthTokens - List auth tokens
func (c *Container) ListAuthTokens(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListRoles - List system defined RBAC roles
func (c *Container) ListRoles(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// Login - Login a user
func (c *Container) Login(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// Logout - Logout a user
func (c *Container) Logout(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
