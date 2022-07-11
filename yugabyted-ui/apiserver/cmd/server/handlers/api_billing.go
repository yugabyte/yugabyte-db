package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// AttachPaymentMethod - Attaches payment method to the stripe customer
func (c *Container) AttachPaymentMethod(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// CreateBillingProfile - This API adds billing profile
func (c *Container) CreateBillingProfile(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// CreateSetupIntent - Create set up intent object
func (c *Container) CreateSetupIntent(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// DeletePaymentMethod - This API deletes payment method
func (c *Container) DeletePaymentMethod(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// EstimateClusterCost - This API to calculate the estimated cost of the cluster
func (c *Container) EstimateClusterCost(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetBillingProfile - This API gets billing profile
func (c *Container) GetBillingProfile(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetDefaultPaymentMethod - Get default payment method
func (c *Container) GetDefaultPaymentMethod(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetRateInfo - Get rate info of an account
func (c *Container) GetRateInfo(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListCredits - Get list of credits for an account
func (c *Container) ListCredits(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListPaymentMethods - Lists billing payment methods
func (c *Container) ListPaymentMethods(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ModifyBillingProfile - This API updates billing profile
func (c *Container) ModifyBillingProfile(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// SetDefaultPaymentMethod - This API sets default payment method
func (c *Container) SetDefaultPaymentMethod(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
