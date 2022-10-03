package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// CreateNetworkAllowList - Create an allow list entity
func (c *Container) CreateNetworkAllowList(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// CreateVpc - Create a dedicated VPC for your DB clusters
func (c *Container) CreateVpc(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// CreateVpcPeering - Create a peering between customer VPC and Yugabyte VPC
func (c *Container) CreateVpcPeering(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// DeleteNetworkAllowList - Delete an allow list entity
func (c *Container) DeleteNetworkAllowList(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// DeleteVpc - Delete customer-facing VPC by ID
func (c *Container) DeleteVpc(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// DeleteVpcPeering - Delete VPC Peering
func (c *Container) DeleteVpcPeering(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetNetworkAllowList - Retrieve an allow list entity
func (c *Container) GetNetworkAllowList(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetSingleTenantVpc - Get customer-facing VPC by ID
func (c *Container) GetSingleTenantVpc(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetVpcPeering - Get a VPC Peering
func (c *Container) GetVpcPeering(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListNetworkAllowLists - Get list of allow list entities
func (c *Container) ListNetworkAllowLists(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListSingleTenantVpcs - Get customer-facing VPCs to choose for cluster isolation
func (c *Container) ListSingleTenantVpcs(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListVpcPeerings - List peerings between customer VPCs and Yugabyte VPCs
func (c *Container) ListVpcPeerings(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
