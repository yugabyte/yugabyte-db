package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// AddCustomImageToSet - API to add a custom image to the specified custom image set
func (c *Container) AddCustomImageToSet(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// CreateCustomImageSetsInBulk - API to create custom image sets in bulk
func (c *Container) CreateCustomImageSetsInBulk(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// DeleteCustomImageSet - Delete custom image set
func (c *Container) DeleteCustomImageSet(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetCustomImageSetDetails - API to get details about custom image set
func (c *Container) GetCustomImageSetDetails(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListCustomImageSets - API to list custom image sets
func (c *Container) ListCustomImageSets(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// MarkCustomImageSetAsDefault - Mark a custom image set as default
func (c *Container) MarkCustomImageSetAsDefault(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
