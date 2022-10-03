package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// AddPlatform - Add new platform
func (c *Container) AddPlatform(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// AddProjectToPlatform - Add project to platform
func (c *Container) AddProjectToPlatform(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetPlatform - Get platform by ID
func (c *Container) GetPlatform(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListPlatforms - List platforms
func (c *Container) ListPlatforms(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// MarkPlatformsForMaintenance - Mark Platforms for Maintenance
func (c *Container) MarkPlatformsForMaintenance(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// RefreshProviderPricing - Refresh pricing in specified existing customer providers
func (c *Container) RefreshProviderPricing(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
