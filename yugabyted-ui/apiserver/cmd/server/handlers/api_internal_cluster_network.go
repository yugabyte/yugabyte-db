package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// EditExternalOrInternalClusterNetworkAllowLists - Modify set of allow lists associated to a cluster
func (c *Container) EditExternalOrInternalClusterNetworkAllowLists(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListAllClusterNetworkAllowLists - Get list of allow list entities associated to a cluster
func (c *Container) ListAllClusterNetworkAllowLists(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
