package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// EditClusterNetworkAllowLists - Modify set of allow lists associated to a cluster
func (c *Container) EditClusterNetworkAllowLists(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListClusterNetworkAllowLists - Get list of allow list entities associated to a cluster
func (c *Container) ListClusterNetworkAllowLists(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
