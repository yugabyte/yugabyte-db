package handlers

import (
    "apiserver/cmd/server/helpers"
    "apiserver/cmd/server/logger"

    "github.com/jackc/pgx/v4/pgxpool"
    "github.com/yugabyte/gocql"
)

// Container will hold all dependencies for your application.
type Container struct {
        logger     logger.Logger
        Client     *gocql.ClusterConfig
        Session    *gocql.Session
        ConnMap    map[helpers.PgClientConnectionParams]*pgxpool.Pool
        helper     helpers.HelperContainer
        serverPort string
}

// NewContainer returns an empty or an initialized container for your handlers.
func NewContainer(
    logger       logger.Logger,
    gocqlClient  *gocql.ClusterConfig,
    gocqlSession *gocql.Session,
    pgxConnMap   map[helpers.PgClientConnectionParams]*pgxpool.Pool,
    helper       helpers.HelperContainer,
    serverPort   string,
    ) (Container, error) {
        c := Container{logger, gocqlClient, gocqlSession, pgxConnMap, helper, serverPort}
        return c, nil
}

func (c *Container) GetSession() (*gocql.Session, error) {
    if c.Session == nil {
        session, err := c.Client.CreateSession()
        if err != nil {
            c.logger.Errorf("Error initializing the gocql session: %s", err.Error())
            return nil, err
        }
        c.Session = session
    }
    return c.Session, nil
}

func (c *Container) GetConnection() (*pgxpool.Pool, error) {
    return c.GetConnectionFromMap(helpers.HOST)
}

func (c *Container) GetConnectionFromMap(host string, database ...string) (*pgxpool.Pool, error) {
    var dbName string
    if len(database) == 0 {
        dbName = helpers.DbName
    } else {
        dbName = database[0]
    }
    pgConnectionParams := helpers.PgClientConnectionParams {
        User:     helpers.DbYsqlUser,
        Password: helpers.DbPassword,
        Host:     host,
        Port:     helpers.YsqlPort,
        Database: dbName,
    }

    conn, exists := c.ConnMap[pgConnectionParams]
    if exists {
        return conn, nil
    }
    // pgConnectionParams := helpers.PgClientConnectionParams {
    //     User:     helpers.DbYsqlUser,
    //     Password: helpers.DbPassword,
    //     Host:     host,
    //     Port:     helpers.PORT,
    //     Database: dbName,
    // }
    conn, err := c.helper.CreatePgClient(c.logger, pgConnectionParams)
    if err != nil {
        c.logger.Errorf("Error initializing the pgx client for Host %s and Database %s: %s",
                            host, dbName, err.Error())
        return conn, err
    }
    c.ConnMap[pgConnectionParams] = conn
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
