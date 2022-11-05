package handlers

import (
        "apiserver/cmd/server/logger"

        "github.com/jackc/pgx/v4"
        "github.com/yugabyte/gocql"
)

// Container will hold all dependencies for your application.
type Container struct {
        logger  logger.Logger
        Session *gocql.Session
        Conn    *pgx.Conn
}

// NewContainer returns an empty or an initialized container for your handlers.
func NewContainer(logger logger.Logger, session *gocql.Session, conn *pgx.Conn) (Container, error) {
        c := Container{logger, session, conn}
        return c, nil
}
