package helpers

import (
    "apiserver/cmd/server/logger"
)

type HelperContainer struct {
        logger  logger.Logger
}

// NewContainer returns an empty or an initialized container for your handlers.
func NewHelperContainer(
    logger       logger.Logger,
    ) (HelperContainer, error) {
        c := HelperContainer{logger}
        return c, nil
}
