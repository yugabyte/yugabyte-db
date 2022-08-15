package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// ArmFaultInjectionForEntity - Arm fault injection
func (c *Container) ArmFaultInjectionForEntity(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// DisarmFaultInjectionForEntity - Disarm fault injection
func (c *Container) DisarmFaultInjectionForEntity(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetEntityRefs - Get list of entity refs for the specified fault
func (c *Container) GetEntityRefs(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetFaultNames - Get fault injections
func (c *Container) GetFaultNames(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
