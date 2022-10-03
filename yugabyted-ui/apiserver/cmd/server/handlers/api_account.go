package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// BatchInviteAccountUser - Batch add or invite user to account
func (c *Container) BatchInviteAccountUser(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// CreateAccount - Create an account
func (c *Container) CreateAccount(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// DeleteAccount - Delete account
func (c *Container) DeleteAccount(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetAccount - Get account info
func (c *Container) GetAccount(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetAccountByName - Get account by name
func (c *Container) GetAccountByName(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetAccountQuotas - Get account quotas
func (c *Container) GetAccountQuotas(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetAccountUser - Get user info
func (c *Container) GetAccountUser(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetAllowedLoginTypes - Get allowed login types for account
func (c *Container) GetAllowedLoginTypes(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// InviteAccountUser - Add or Invite user to account
func (c *Container) InviteAccountUser(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListUsers - List users
func (c *Container) ListUsers(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ModifyAccount - Modify account
func (c *Container) ModifyAccount(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ModifyAllowedLoginTypes - Modify allowed login types for account
func (c *Container) ModifyAllowedLoginTypes(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ModifyUserRole - Modify user role
func (c *Container) ModifyUserRole(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// RemoveAccountUser - Remove user from account
func (c *Container) RemoveAccountUser(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
