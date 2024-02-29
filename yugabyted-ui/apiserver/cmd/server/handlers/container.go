package handlers

import (
    "apiserver/cmd/server/helpers"
    "apiserver/cmd/server/logger"

    "fmt"
    "net"
    "sync"

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
        gocqlLock  sync.Mutex
        pgxLock    sync.Mutex
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
        c := Container{logger, gocqlClient, gocqlSession, pgxConnMap, helper, serverPort,
            sync.Mutex{}, sync.Mutex{}}
        return c, nil
}

func (c *Container) GetSession() (*gocql.Session, error) {
    c.gocqlLock.Lock()
    defer c.gocqlLock.Unlock()
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
// The tserver address list is determined by the cached value TserverAddressCache.
// This function will update the cached value if it fails to get a pgx connection.
func (c *Container) GetConnection(database ...string) (*pgxpool.Pool, error) {
    var dbName string
    if len(database) == 0 {
        dbName = helpers.DbName
    } else {
        dbName = database[0]
    }
    // Try to use cached tserver addresses
    tserverAddressCache := helpers.TserverAddressCache.Get()
    c.logger.Debugf("got cached tserver addresses %v", tserverAddressCache)
    conn, successHost, err := c.GetConnectionFromList(dbName, tserverAddressCache)
    if err == nil {
        // Move failed addresses to back of cache
        for index, host := range tserverAddressCache {
            if successHost == host {
                tserverAddressCache =
                    append(tserverAddressCache[index:], tserverAddressCache[:index]...)
                break
            }
        }
        helpers.TserverAddressCache.Update(tserverAddressCache)
        c.logger.Debugf("updated cached tserver addresses %v", tserverAddressCache)
        return conn, err
    }
    c.logger.Warnf("attempt to get pgx connection from all cached tserver addresses failed")
    // Get tserver addresses. This triggers a cache refresh.
    hostNames, err := c.getNodes()
    if err != nil {
        c.logger.Errorf("failed to get list of tservers")
        return nil, err
    }
    for index, host := range hostNames {
        if host == helpers.HOST {
            // Swap helpers.HOST to front of slice
            hostNames[0], hostNames[index] = hostNames[index], hostNames[0]
            break
        }
    }
    c.logger.Debugf("got new tserver addresses %v", hostNames)
    conn, _, err = c.GetConnectionFromList(dbName, hostNames)
    if err == nil {
        return conn, err
    }
    c.logger.Errorf("attempt to get pgx connection from tserver addresses failed")
    err = fmt.Errorf("failed to get pgx connection from tserver addresses %v", hostNames)
    return nil, err
}

// Gets a pgx connection from the provided list, and returns the address of the returned connection
func (c *Container) GetConnectionFromList(
    dbName string,
    hosts []string,
) (*pgxpool.Pool, string, error) {
    for _, hostName := range hosts {
        conn, err := c.GetConnectionFromMap(hostName, dbName)
        if err == nil {
            return conn, hostName, nil
        } else {
            c.logger.Warnf("failed to get pgx connection for address %s: %s", hostName, err.Error())
        }
    }
    c.logger.Errorf("attempt to get pgx connection from tserver addresses failed")
    err := fmt.Errorf("failed to get pgx connection from tserver addresses %v", hosts)
    return nil, "", err
}

func (c *Container) GetConnectionFromMap(host string, database ...string) (*pgxpool.Pool, error) {
    c.pgxLock.Lock()
    defer c.pgxLock.Unlock()
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
        c.logger.Debugf("acquired existing pgxpool connection to %s", host)
        return conn, nil
    }
    conn, err := c.helper.CreatePgClient(c.logger, pgConnectionParams)
    if err != nil {
        c.logger.Errorf("Error initializing the pgx client for Host %s and Database %s: %s",
                            host, dbName, err.Error())
        return conn, err
    }
    c.ConnMap[pgConnectionParams] = conn
    c.logger.Debugf("created pgxpool connection to %s", host)
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
