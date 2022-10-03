package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// GetCACert - Get certificate for connection to the cluster
func (c *Container) GetCACert(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetClusterTierSpecs - Get base prices and specs of free and paid tier clusters
func (c *Container) GetClusterTierSpecs(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetInstanceTypes - Get the list of supported instance types for a given region/zone and provider
func (c *Container) GetInstanceTypes(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetRegions - Retrieve list of regions available to deploy cluster by cloud
func (c *Container) GetRegions(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
