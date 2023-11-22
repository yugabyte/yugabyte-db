package main

import (
    "apiserver/cmd/server/handlers"
    "apiserver/cmd/server/helpers"
    "apiserver/cmd/server/logger"
    "apiserver/cmd/server/templates"
    "embed"
    "io/fs"
    "net/http"
    "os"
    "strconv"
    "time"

    "html/template"

    "github.com/jackc/pgx/v4/pgxpool"
    "github.com/labstack/echo/v4"
    "github.com/labstack/echo/v4/middleware"
)

const serverPortEnv string = "YUGABYTED_UI_PORT"
const logLevelEnv string = "YUGABYTED_UI_LOG_LEVEL"

const (
    uiDir     = "ui"
    extension = "/*.html"
)

//go:embed ui
var staticFiles embed.FS

var templatesMap map[string]*template.Template

func getEnv(key, fallback string) string {
    if value, ok := os.LookupEnv(key); ok {
        return value
    }
    return fallback
}

func LoadTemplates() error {

    if templatesMap == nil {
        templatesMap = make(map[string]*template.Template)
    }

    templateFiles, err := fs.ReadDir(staticFiles, uiDir)
    if err != nil {
        return err
    }

    for _, tmpl := range templateFiles {
        if tmpl.IsDir() {
            continue
        }

        file, err := template.ParseFS(staticFiles, "ui/index.html")
        if err != nil {
            return err
        }

        templatesMap[tmpl.Name()] = file
    }
    return nil
}

func getStaticFiles() http.FileSystem {

    println("using embed mode")
    fsys, err := fs.Sub(staticFiles, "ui")
    if err != nil {
        panic(err)
    }

    return http.FS(fsys)
}

func main() {

    // Initialize logger
    logLevel := getEnv(logLevelEnv, "info")
    var logLevelEnum logger.LogLevel
    switch logLevel {
    case "debug":
        logLevelEnum = logger.Debug
    case "info":
        logLevelEnum = logger.Info
    case "warn":
        logLevelEnum = logger.Warn
    case "error":
        logLevelEnum = logger.Error
    default:
        println("unknown log level env variable, defaulting to info level logging")
        logLevel = "info"
        logLevelEnum = logger.Info
    }
    log, _ := logger.NewLogger(logLevelEnum)
    defer log.Cleanup()
    log.Infof("Logger initialized with %s level logging", logLevel)

    // all helper functions are be methods of the helper object
    helper, _ := helpers.NewHelperContainer(log)

    serverPort := getEnv(serverPortEnv, "15433")

    port := ":" + serverPort

    LoadTemplates()

    e := echo.New()

    cluster := helper.CreateGoCqlClient(log)

    // We keep a map of pgx connections since we need to
    // connect to all nodes (sql) for slow_queries
    pgxConnMap := map[helpers.PgClientConnectionParams]*pgxpool.Pool{}

    // We don't actually need a pgx connection at startup
    // The only place we use pgx connection is in slow_queries
    // where we need a connection to every node in the cluster.
    // The connections will be made when a request to slow_queries
    // is made.
    // We can uncomment the code below if we ever need to make sql
    // queries to one node more immediately, eg. on the overview page

    // pgxConn, err := helpers.CreatePgClient(log, helpers.HOST)
    // if err != nil {
    //     // In case of failure, set to nil
    //     // We will try to set up a connection again later
    //     log.Errorf("Error initializing the pgx client.")
    //     log.Errorf(err.Error())
    //     pgxConn = nil
    // } else {
    //     pgxConnMap[helpers.HOST] = pgxConn
    // }

    gocqlSession, err := cluster.CreateSession()
    if err != nil {
        // In case of failure, set to nil
        // We will try to set up a connection again later
        log.Errorf("Error initializing the gocql session.")
        log.Errorf(err.Error())
        gocqlSession = nil
    }

    //todo: handle the error!
    c, _ := handlers.NewContainer(log, cluster, gocqlSession, pgxConnMap, helper, serverPort)
    defer c.Cleanup()

    // Middleware
    e.Use(middleware.CORS())
    e.Use(middleware.RecoverWithConfig(middleware.RecoverConfig{
        LogErrorFunc: func(c echo.Context, err error, stack []byte) error {
            log.Errorf("[PANIC RECOVER] %v %s\n", err, stack)
            return nil
        },
    }))
    e.Use(middleware.RequestLoggerWithConfig(middleware.RequestLoggerConfig{
        LogURI:           true,
        LogStatus:        true,
        LogLatency:       true,
        LogMethod:        true,
        LogContentLength: true,
        LogResponseSize:  true,
        LogUserAgent:     true,
        LogHost:          true,
        LogRemoteIP:      true,
        LogRequestID:     true,
        LogError:         true,
        LogValuesFunc: func(c echo.Context, v middleware.RequestLoggerValues) error {
            bytes_in, err := strconv.ParseInt(v.ContentLength, 10, 64)
            if err != nil {
                bytes_in = 0
            }
            err = v.Error
            errString := ""
            if err != nil {
                errString = err.Error()
            }
            log.With(
                "time", v.StartTime.Format(time.RFC3339Nano),
                "id", v.RequestID,
                "remote_ip", v.RemoteIP,
                "host", v.Host,
                "method", v.Method,
                "URI", v.URI,
                "user_agent", v.UserAgent,
                "status", v.Status,
                "error", errString,
                "latency", v.Latency.Nanoseconds(),
                "latency_human", time.Duration(v.Latency.Microseconds()).String(),
                "bytes_in", bytes_in,
                "bytes_out", v.ResponseSize,
            ).Infof(
                "request",
            )
            return nil
        },
    }))

    // Middleware to redirect all non-api calls to index.html, to allow direct navigation to
    // UI pages by URL path. Need to add to this list when creating new paths in the UI.
    e.Use(middleware.Rewrite(map[string]string{
        "^/alerts*": "/",
        "^/databases*": "/",
        "^/debug*": "/",
        "^/migrations*": "/",
        "^/performance*": "/",
    }))

    // GetCluster - Get a cluster
    e.GET("/api/cluster", c.GetCluster)

    // GetClusterMetric - Get a metric for a cluster
    e.GET("/api/metrics", c.GetClusterMetric)

    // GetClusterActivities - Get activity data of a cluster
    e.GET("/api/activities", c.GetClusterActivities)

    // GetClusterNodes - Get the nodes for a cluster
    e.GET("/api/nodes", c.GetClusterNodes)

    // GetHealthCheck - Get health information about the cluster
    e.GET("/api/health-check", c.GetClusterHealthCheck)

    // GetClusterTables - Get list of DB tables per YB API (YCQL/YSQL)
    e.GET("/api/tables", c.GetClusterTables)

    // GetLiveQueries - Get the live queries in a cluster
    e.GET("/api/live_queries", c.GetLiveQueries)

    // GetSlowQueries - Get the slow queries in a cluster
    e.GET("/api/slow_queries", c.GetSlowQueries)

    // GetClusterTablets - Get list of tablets
    e.GET("/api/tablets", c.GetClusterTablets)

    // GetVersion - Get YugabyteDB version
    e.GET("/api/version", c.GetVersion)

    // GetIsLoadBalancerIdle - Check if cluster load balancer is idle
    e.GET("/api/is_load_balancer_idle", c.GetIsLoadBalancerIdle)

    // GetGflagsJson - Retrieve the gflags from Master and Tserver process
    e.GET("/api/gflags", c.GetGflagsJson)

    // GetClusterAlerts - Get list of any current cluster alerts
    e.GET("/api/alerts", c.GetClusterAlerts)

    // GetTableInfo - Get info on a single table, given table uuid
    e.GET("/api/table", c.GetTableInfo)

    // Get Voyager migrations info
    e.GET("/api/migrations", c.GetVoyagerMigrations)

    // Get Voyager assement info
    e.GET("/api/migration_assesment", c.GetVoyagerAssesmentDetails)

    // Get Migrate schema task details
    e.GET("/api/migrate_schema", c.GetMigrateSchemaInfo)

    // Get Voyager Data migration metrics
    e.GET("/api/migration_metrics", c.GetVoyagerMetrics)

    // GetClusterConnections - Get YSQL connection manager stats for every node of the cluster
    e.GET("/api/connections", c.GetClusterConnections)

    // GetClusterConnections - Get the node address for the current node
    e.GET("/api/node_address", c.GetNodeAddress)

    render_htmls := templates.NewTemplate()

    // Code for rendering UI Without embedding the files
    // render_htmls.Add("index.html", template.Must(template.ParseGlob("ui/index.html")))
    // e.Static("/", "ui")
    // e.Renderer = render_htmls
    // e.GET("/", handlers.IndexHandler)

    render_htmls.Add("index.html", templatesMap["index.html"])
    assetHandler := http.FileServer(getStaticFiles())
    e.GET("/*", echo.WrapHandler(http.StripPrefix("/", assetHandler)))
    e.Renderer = render_htmls
    e.GET("/", handlers.IndexHandler)

    // Start server
    uiBindAddress := helpers.HOST + port
    e.Logger.Fatal(e.Start(uiBindAddress))
}
