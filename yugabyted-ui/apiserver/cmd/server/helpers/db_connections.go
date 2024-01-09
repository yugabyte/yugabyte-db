package helpers

import (
    "apiserver/cmd/server/logger"
    "context"
    "fmt"
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

    // Initialize gocql client
    cluster := gocql.NewCluster(fmt.Sprintf("%s:%d", HOST, YcqlPort))

    if Secure {
            cluster.Authenticator = gocql.PasswordAuthenticator{
                    Username: DbYcqlUser,
                    Password: DbPassword,
            }
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
