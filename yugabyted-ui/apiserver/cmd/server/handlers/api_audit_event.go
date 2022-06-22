package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// GetAuditEventById - Get detailed information about a specific audit log event
func (c *Container) GetAuditEventById(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetAuditEventCategories - Get audit event categories
func (c *Container) GetAuditEventCategories(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListAuditEvents - Get list of audit events for a given account
func (c *Container) ListAuditEvents(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
