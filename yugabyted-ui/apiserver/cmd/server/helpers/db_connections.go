package helpers

import (
    "apiserver/cmd/server/logger"
    "context"
    "fmt"
    "net"
    "time"

    "github.com/yugabyte/gocql"
    "github.com/jackc/pgx/v4/pgxpool"
)

type PgClientConnectionParams struct {
        User     string
        Password string
        Host     string
        Port     int
        Database string
}

func (h *HelperContainer) CreateGoCqlClient(log logger.Logger) *gocql.ClusterConfig {

    // Get list of tservers
    hostNames := []string{}
    tabletServersFuture := make(chan TabletServersFuture)
    go h.GetTabletServersFuture(HOST, tabletServersFuture)
    tabletServersResponse := <-tabletServersFuture
    if tabletServersResponse.Error != nil {
        log.Warnf("failed to get list of tservers for gocql client setup: %s",
            tabletServersResponse.Error)
        // If we fail to get tservers from GetTabletServersFuture, use HOST
        hostNames = append(hostNames, fmt.Sprintf("%s:%d", HOST, YcqlPort))
    } else {
        // to get hostnames, get all second level keys and only keep if
        // net.SpliHostPort succeeds.
        for _, obj := range tabletServersResponse.Tablets {
            for hostport := range obj {
                host, _, err := net.SplitHostPort(hostport)
                if err != nil {
                    log.Warnf("failed to split hostport %s: %s", hostport, err.Error())
                } else {
                    hostNames = append(hostNames, fmt.Sprintf("%s:%d", host, YcqlPort))
                }
            }
        }
    }
    log.Infof("initializing gocql client with initial addresses: %v", hostNames)
    // Initialize gocql client
    cluster := gocql.NewCluster(hostNames...)

    cluster.Authenticator = gocql.PasswordAuthenticator{
        Username: DbYcqlUser,
        Password: DbYcqlPassword,
    }

    if Secure {
            cluster.SslOpts = &gocql.SslOptions{
                    CaPath: SslRootCert,
            }
    }

    // Use the same timeout as the Java driver.
    cluster.Timeout = 12 * time.Second

    // Create the session.
    log.Debugf("Initializing gocql client.")

    return cluster
}

func (h *HelperContainer) CreatePgClient(log logger.Logger,
        connectionParams PgClientConnectionParams) (*pgxpool.Pool, error) {

    var url string

    url = fmt.Sprintf("postgres://%s:%s@%s:%d/%s",
            connectionParams.User, connectionParams.Password, connectionParams.Host,
            connectionParams.Port, connectionParams.Database)
    if Secure {
            secureOptions := fmt.Sprintf("sslmode=%s", SslMode)
            if SslRootCert != "" {
                    secureOptions = fmt.Sprintf("%s&%s", secureOptions, SslRootCert)
            }
            url = fmt.Sprintf("%s?%s", url, secureOptions)
    }

    log.Debugf("Initializing pgx client.")
    conn, err := pgxpool.Connect(context.Background(), url)
    return conn, err
}
