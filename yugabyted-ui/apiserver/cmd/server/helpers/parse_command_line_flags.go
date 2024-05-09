package helpers

import (
    "flag"
    "os"
    "path/filepath"
)

var (
    HOST           string
    YsqlPort       int
    YcqlPort       int
    BindAddr       string
    Secure         bool
    DbName         string
    DbYsqlUser     string
    DbYcqlUser     string
    DbYsqlPassword string
    DbYcqlPassword string
    SslMode        string
    SslRootCert    string
    MasterUIPort   string
    TserverUIPort  string
    Warnings       string
    DataDir        string
)

func init() {
    homeDir, _ := os.UserHomeDir()
    defaultDataDir := filepath.Join(homeDir, "var", "data")
    flag.StringVar(&HOST, "database_host", "127.0.0.1",
        "Advertise address of the local YugabyteDB node.")
    flag.StringVar(&DataDir, "data_dir", defaultDataDir, "Path to the data directory")
    flag.IntVar(&YsqlPort, "ysql_port", 5433, "YSQL port.")
    flag.IntVar(&YcqlPort, "ycql_port", 9042, "YCQL port.")
    flag.StringVar(&BindAddr, "bind_address", "",
        "Bind address for the yugabyted UI. If not set, defaults to the value of database_host.")
    flag.BoolVar(&Secure, "secure", false, "API server use secure or insecure mode.")
    flag.StringVar(&DbName, "database_name", "yugabyte", "database for retrieving metrics.")
    flag.StringVar(&DbYsqlUser, "ysql_username", "yugabyte",
        "username for connecting to ysql.")
    flag.StringVar(&DbYcqlUser, "ycql_username", "cassandra",
        "username for connecting to ycql.")
    flag.StringVar(&DbYsqlPassword, "ysql_password", "yugabyte",
        "password for connecting to the ysql database.")
    flag.StringVar(&DbYcqlPassword, "ycql_password", "yugabyte",
        "password for connecting to the ycql database.")
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
