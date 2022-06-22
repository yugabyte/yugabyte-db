package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// ListAlertNotifications - API to fetch the alert notifications for an account
func (c *Container) ListAlertNotifications(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListAlertRules - API to fetch the alert rules for an account
func (c *Container) ListAlertRules(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// SendTestEmailAlert - API to send test email alerts to users of an account
func (c *Container) SendTestEmailAlert(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// UpdateAlertRule - API to modify alert rule for an account
func (c *Container) UpdateAlertRule(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
