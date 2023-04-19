package helpers

import (
    "apiserver/cmd/server/logger"
    "context"
    "fmt"
    "time"

    "github.com/yugabyte/gocql"
    "github.com/jackc/pgx/v4/pgxpool"
)

func CreateGoCqlClient(log logger.Logger) *gocql.ClusterConfig {

    // Initialize gocql client
    cluster := gocql.NewCluster(HOST)

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

func CreatePgClient(log logger.Logger, host string) (*pgxpool.Pool, error) {

    var url string

    url = fmt.Sprintf("postgres://%s:%s@%s:%d/%s",
            DbYsqlUser, DbPassword, host, PORT, DbName)
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
