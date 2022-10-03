package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// GetAppConfig - Get application configuration
func (c *Container) GetAppConfig(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetSsoRedirectUrl - Retrieve redirect URL for Single Sign On using external authentication.
func (c *Container) GetSsoRedirectUrl(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetUserTutorials - Get tutorials for a user
func (c *Container) GetUserTutorials(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// SsoInviteCallback - Callback for SSO invite
func (c *Container) SsoInviteCallback(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// SsoLoginCallback - Callback for SSO login
func (c *Container) SsoLoginCallback(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// SsoSignupCallback - Callback for SSO signup
func (c *Container) SsoSignupCallback(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// UpdateUserTutorial - Update tutorial for a user
func (c *Container) UpdateUserTutorial(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// UpdateUserTutorialEnabled - Update whether tutorial is enabled for a user
func (c *Container) UpdateUserTutorialEnabled(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// UpdateUserTutorialState - Update tutorial state status for a user
func (c *Container) UpdateUserTutorialState(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
