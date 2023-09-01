package helpers

import (
    "flag"
)

var (
    HOST          string
    PORT          int
    Secure        bool
    DbName        string
    DbYsqlUser    string
    DbYcqlUser    string
    DbPassword    string
    SslMode       string
    SslRootCert   string
    MasterUIPort  string
    TserverUIPort string
    Warnings      string
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
    flag.StringVar(&MasterUIPort, "master_ui_port", "7000",
        "Master UI port.")
    flag.StringVar(&TserverUIPort, "tserver_ui_port", "9000",
        "Tserver UI port.")
    flag.StringVar(&Warnings, "warnings", "", "Warnings from the Pre-reqs check by the CLI.")
    flag.Parse()
}
