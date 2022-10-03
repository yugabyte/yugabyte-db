package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// AddNetwork - Add new cluster network
func (c *Container) AddNetwork(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// CreateInternalNetworkAllowList - Create a private allow list entity
func (c *Container) CreateInternalNetworkAllowList(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// CreateInternalVpcPeering - Peer two yugabyte VPC
func (c *Container) CreateInternalVpcPeering(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// CreateSingleTenantVpcMetadata - Create customer-facing VPC metadata for cluster isolation
func (c *Container) CreateSingleTenantVpcMetadata(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// DeleteExternalOrInternalNetworkAllowList - Delete an allow list entity
func (c *Container) DeleteExternalOrInternalNetworkAllowList(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// DeleteInternalVpcPeering - Delete internal VPC peering between two yugabyte VPC
func (c *Container) DeleteInternalVpcPeering(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetExternalOrInternalNetworkAllowList - Retrieve an allow list entity
func (c *Container) GetExternalOrInternalNetworkAllowList(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetVpc - Get network info by ID
func (c *Container) GetVpc(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListAllNetworkAllowLists - Get list of allow list entities
func (c *Container) ListAllNetworkAllowLists(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListNetworks - List all cluster networks
func (c *Container) ListNetworks(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// MarkVpcsForMaintenance - Mark VPCs for Maintenance
func (c *Container) MarkVpcsForMaintenance(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
