package handlers
import (
    "apiserver/cmd/server/models"
    "github.com/labstack/echo/v4"
    "net/http"
)

// BatchAddTracks - Add release tracks to account
func (c *Container) BatchAddTracks(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// CreateRelease - Create a software release
func (c *Container) CreateRelease(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// CreateTrack - Create a DB software release track
func (c *Container) CreateTrack(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// DeleteRelease - Delete a software release
func (c *Container) DeleteRelease(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// DeleteTrack - Delete a DB software release track
func (c *Container) DeleteTrack(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListReleasesOnTrack - List all DB software releases by track
func (c *Container) ListReleasesOnTrack(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// ListTracks - List all DB software release tracks
func (c *Container) ListTracks(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// RemoveTrack - Remove release track from account
func (c *Container) RemoveTrack(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}


// UpdateRelease - Update a software release
func (c *Container) UpdateRelease(ctx echo.Context) error {
    return ctx.JSON(http.StatusOK, models.HelloWorld {
        Message: "Hello World",
    })
}
