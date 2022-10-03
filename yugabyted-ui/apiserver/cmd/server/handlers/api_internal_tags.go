package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// GetAllowedValuesForInternalTags - API to fetch allowed values for internal tags
func (c *Container) GetAllowedValuesForInternalTags(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetUserInternalTags - API to get user internal tags for a given user
func (c *Container) GetUserInternalTags(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListAllDefaultInternalTags - API to fetch all the default internal tags
func (c *Container) ListAllDefaultInternalTags(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// UpdateDefaultInternalTags - API to batch set/update default internal tags
func (c *Container) UpdateDefaultInternalTags(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// UpdateUserInternalTags - API to set/update internal tags for a given user
func (c *Container) UpdateUserInternalTags(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
