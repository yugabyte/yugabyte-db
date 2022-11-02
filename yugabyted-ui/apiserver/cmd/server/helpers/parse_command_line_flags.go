package helpers

import (
        "flag"
)

var (
        HOST        string
        PORT        int
        Secure      bool
        DbName      string
        DbYsqlUser  string
        DbYcqlUser  string
        DbPassword  string
        SslMode     string
        SslRootCert string
)

func init() {
        flag.StringVar(&HOST, "database_host", "127.0.0.1",
                "Advertise address of the local YugabyteDB node.")
        flag.IntVar(&PORT, "database_port", 5433, "YSQL port.")
        flag.BoolVar(&Secure, "secure", false, "API server use secure or insecure mode.")
        flag.StringVar(&DbName, "database_name", "yugabyte", "database for retrieving metrics.")
        flag.StringVar(&DbYsqlUser, "ysql_username", "yugabyte",
                "username for connecting to ysql.")
        flag.StringVar(&DbYcqlUser, "ycql_username", "cassandra",
                "username for connecting to ycql.")
        flag.StringVar(&DbPassword, "database_password", "yugabyte",
                "password for connecting to the database.")
        flag.StringVar(&SslMode, "ssl_mode", "require",
                "ssl mode for connecting to the database.")
        flag.StringVar(&SslRootCert, "ssl_root certificate", "",
                "root certificate for connecting to the database.")
        flag.Parse()
}
