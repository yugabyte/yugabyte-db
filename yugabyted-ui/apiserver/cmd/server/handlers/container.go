package handlers

import (
    "apiserver/cmd/server/helpers"
    "apiserver/cmd/server/logger"
    "net"

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
        // If the session wasn't created yet, it could be because the initial gocql cluster config
        // couldn't get tservers from the master because it wasn't set up yet, so try again
        // Get list of tservers
        hostNames := []string{}
        tabletServersFuture := make(chan helpers.TabletServersFuture)
        go c.helper.GetTabletServersFuture(helpers.HOST, tabletServersFuture)
        tabletServersResponse := <-tabletServersFuture
        if tabletServersResponse.Error != nil {
            c.logger.Warnf("failed to get list of tservers for gocql client setup: %s",
                tabletServersResponse.Error)
            // If we fail to get tservers from GetTabletServersFuture, use HOST
            hostNames = append(hostNames, helpers.HOST)
        } else {
            // to get hostnames, get all second level keys and only keep if
            // net.SpliHostPort succeeds.
            for _, obj := range tabletServersResponse.Tablets {
                for hostport := range obj {
                    host, _, err := net.SplitHostPort(hostport)
                    if err != nil {
                        c.logger.Warnf("failed to split hostport %s: %s", hostport, err.Error())
                    } else {
                        hostNames = append(hostNames, host)
                    }
                }
            }
        }
        c.logger.Infof("updating gocql client with new addresses: %v", hostNames)
        c.Client.Hosts = hostNames

        session, err := c.Client.CreateSession()
        if err != nil {
            c.logger.Errorf("Error initializing the gocql session: %s", err.Error())
            return nil, err
        }
        c.Session = session
    }
    return c.Session, nil
}

// Gets a single pgx connection to an arbitrary tserver.
// The tserver address is determined by the cached value TserverAddressCache.
// This function will update the cached value if it fails to get a pgx connection.
func (c *Container) GetConnection(database ...string) (*pgxpool.Pool, error) {
    var dbName string
    if len(database) == 0 {
        dbName = helpers.DbName
    } else {
        dbName = database[0]
    }
    // Try to use cached tserver address
    hostName := helpers.TserverAddressCache.Get()

    // Use the session from the context.
    conn, err := c.GetConnectionFromMap(hostName, dbName)
    if err != nil {
        c.logger.Warnf("attempt to get pgx connection from cached tserver address failed")
        // Try to get a new tserver address, but prefer helpers.HOST if it is a tserver
        hostName = helpers.HOST
        // Reset tserver cache as well to favor helpers.HOST
        helpers.TserverAddressCache.Update(hostName)
        tserverAddresses, err := c.getNodes()
        if err != nil {
            c.logger.Errorf("failed to get list of tservers")
            return nil, err
        } else {
            contains := false
            for _, host := range tserverAddresses {
                if host == hostName {
                    contains = true
                    break
                }
            }
            if !contains {
                hostName = tserverAddresses[0]
            }
        }
        conn, err = c.GetConnectionFromMap(hostName, dbName)
        if err != nil {
            c.logger.Errorf("attempt to update cached tserver address failed")
            return nil, err
        }
        // Update tserver cache if connection attempt successful
        helpers.TserverAddressCache.Update(hostName)
    }
    return conn, nil
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
        Password: helpers.DbYsqlPassword,
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
