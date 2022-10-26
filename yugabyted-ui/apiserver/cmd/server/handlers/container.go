package handlers
import (
    "apiserver/cmd/server/logger"
)
// Container will hold all dependencies for your application.
type Container struct {
    logger logger.Logger
}

// NewContainer returns an empty or an initialized container for your handlers.
func NewContainer(logger logger.Logger) (Container, error) {
    c := Container{logger}
    return c, nil
}
