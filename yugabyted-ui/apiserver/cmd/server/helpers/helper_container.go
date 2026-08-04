package helpers

import (
    "apiserver/cmd/server/logger"
)

type HelperContainer struct {
        logger  logger.Logger
}

// NewHelperContainer returns an initialized container for your helpers.
func NewHelperContainer(
    logger       logger.Logger,
    ) (HelperContainer, error) {
        c := HelperContainer{logger}
        // initialize cache variables
        c.InitCache()
        return c, nil
}
