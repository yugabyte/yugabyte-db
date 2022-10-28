package helpers

import (
        "flag"
)

var (
        HOST string
        PORT int
)

func init() {
        flag.StringVar(&HOST, "database_host", "127.0.0.1",
                              "Advertise address of the local YugabyteDB node.")
        flag.IntVar(&PORT, "database_port", 5433, "YSQL port")
        flag.Parse()
}
