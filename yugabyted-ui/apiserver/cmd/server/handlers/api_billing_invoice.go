package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// GetBillingInvoiceSummary - Billing invoice summary
func (c *Container) GetBillingInvoiceSummary(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetBillingInvoiceSummaryByInvoiceId - Billing invoice summary by invoice id
func (c *Container) GetBillingInvoiceSummaryByInvoiceId(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetUsageSummary - Get account's summary usage
func (c *Container) GetUsageSummary(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetUsageSummaryStatistics - Get account's summary usage statistics
func (c *Container) GetUsageSummaryStatistics(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListInvoices - Get list of invoices for an account
func (c *Container) ListInvoices(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
