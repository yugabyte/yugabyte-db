package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// CreatePrivateCluster - Create a Private cluster
func (c *Container) CreatePrivateCluster(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// DbUpgrade - Submit task to upgrade DB version of a cluster
func (c *Container) DbUpgrade(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// EditPrivateCluster - Submit task to edit a private cluster
func (c *Container) EditPrivateCluster(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetClusterInternalDetails - Get a cluster
func (c *Container) GetClusterInternalDetails(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetDbReleases - Get all the available DB releases for upgrade
func (c *Container) GetDbReleases(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GetPlatformForCluster - Get data of platform which manages the given cluster
func (c *Container) GetPlatformForCluster(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// GflagsUpgrade - Submit task to upgrade gflags of a cluster
func (c *Container) GflagsUpgrade(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListGFlags - List all GFlags on a cluster
func (c *Container) ListGFlags(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// LockClusterForSupport - Acquire lock on the cluster
func (c *Container) LockClusterForSupport(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// RebuildScrapeTargets - Rebuild prometheus configmap for scrape targets
func (c *Container) RebuildScrapeTargets(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// UnlockClusterForSupport - Release lock on the cluster
func (c *Container) UnlockClusterForSupport(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// VmUpgrade - Submit task to upgrade VM image of a cluster
func (c *Container) VmUpgrade(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
