package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// AddCreditToBillingAccount - API to add credits to the given account
func (c *Container) AddCreditToBillingAccount(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// Aggregate - Run daily billing aggregation
func (c *Container) Aggregate(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// CreateRateCard - Creates rate card for the account
func (c *Container) CreateRateCard(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// DeleteInvoice - Delete billing invoice
func (c *Container) DeleteInvoice(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GenerateInvoice - Generate an invoice for the account
func (c *Container) GenerateInvoice(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// SetAutomaticInvoiceGeneration - Enable or disable automatic invoice generation
func (c *Container) SetAutomaticInvoiceGeneration(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// UpdateBillingInvoice - Update billing invoice
func (c *Container) UpdateBillingInvoice(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// UpdateCreditsForAccount - API to update credits for the given account
func (c *Container) UpdateCreditsForAccount(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// UpdateGlobalRateCard - Updates global rate card
func (c *Container) UpdateGlobalRateCard(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// UpdatePaymentMethod - API to update billing method to OTHER/EMPLOYEE
func (c *Container) UpdatePaymentMethod(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
