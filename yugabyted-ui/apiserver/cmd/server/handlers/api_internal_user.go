package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// ActivateInvitedUserWithoutToken - Activate invited user by skipping token validation
func (c *Container) ActivateInvitedUserWithoutToken(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ActivateSignupUserWithoutToken - Activate signup user by skipping token validation
func (c *Container) ActivateSignupUserWithoutToken(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// CleanupUser - Delete user and remove the accounts/projects of which they are the sole admin
func (c *Container) CleanupUser(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListAllUsers - List all users
func (c *Container) ListAllUsers(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
