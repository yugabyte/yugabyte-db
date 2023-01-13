package handlers

import (
    "apiserver/cmd/server/helpers"
    "apiserver/cmd/server/logger"

    "github.com/jackc/pgx/v4/pgxpool"
    "github.com/yugabyte/gocql"
)

// Container will hold all dependencies for your application.
type Container struct {
        logger  logger.Logger
        Client  *gocql.ClusterConfig
        Session *gocql.Session
        ConnMap map[string]*pgxpool.Pool
}

// NewContainer returns an empty or an initialized container for your handlers.
func NewContainer(
    logger       logger.Logger,
    gocqlClient  *gocql.ClusterConfig,
    gocqlSession *gocql.Session,
    pgxConnMap   map[string]*pgxpool.Pool,
    ) (Container, error) {
        c := Container{logger, gocqlClient, gocqlSession, pgxConnMap}
        return c, nil
}

func (c *Container) GetSession() (*gocql.Session, error) {
    if c.Session == nil {
        session, err := c.Client.CreateSession()
        if err != nil {
            c.logger.Errorf("Error initializing the gocql session")
            return nil, err
        }
        c.Session = session
    }
    return c.Session, nil
}

func (c *Container) GetConnection() (*pgxpool.Pool, error) {
    return c.GetConnectionFromMap(helpers.HOST)
}

func (c *Container) GetConnectionFromMap(host string) (*pgxpool.Pool, error) {
    conn, exists := c.ConnMap[host]
    if exists {
        return conn, nil
    }
    conn, err := helpers.CreatePgClient(c.logger, host)
    if err != nil {
        c.logger.Errorf("Error initializing the pgx client.")
        return conn, err
    }
    c.ConnMap[host] = conn
    return conn, nil
}

func (c *Container) Cleanup() {
    if c.Session != nil {
        c.Session.Close()
    }
    for _, conn := range c.ConnMap {
        conn.Close()
    }
}
