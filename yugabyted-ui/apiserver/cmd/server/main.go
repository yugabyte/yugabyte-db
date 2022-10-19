package main

import (
  "apiserver/cmd/server/handlers"
  "apiserver/cmd/server/templates"
  "apiserver/cmd/server/logger"
  "embed"
  "io/fs"
  "net/http"
  "os"
  "strconv"
  "time"

  "html/template"

  "github.com/labstack/echo/v4"
  "github.com/labstack/echo/v4/middleware"
)

const serverPortEnv string = "YUGABYTED_UI_PORT"

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
  var log logger.Logger
  log, _ = logger.NewSugaredLogger()
  defer log.Cleanup()
  log.Infof("Logger initialized")

  serverPort := getEnv(serverPortEnv, "15433")

  port := ":" + serverPort

  LoadTemplates()

  e := echo.New()

  //todo: handle the error!
  c, _ := handlers.NewContainer(log)

  // Middleware
  e.Use(middleware.RecoverWithConfig(middleware.RecoverConfig{
    LogErrorFunc: func(c echo.Context, err error, stack []byte) error {
      log.Errorf("[PANIC RECOVER] %v %s\n", err, stack)
      return nil
    },
  }))
  e.Use(middleware.RequestLoggerWithConfig(middleware.RequestLoggerConfig{
    LogURI:    true,
    LogStatus: true,
    LogLatency: true,
    LogMethod: true,
    LogContentLength: true,
    LogResponseSize: true,
    LogUserAgent: true,
    LogHost: true,
    LogRemoteIP: true,
    LogRequestID: true,
    LogError: true,
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

  // GetCluster - Get a cluster
  e.GET("/api/cluster", c.GetCluster)

  // GetClusterMetric - Get a metric for a cluster
  e.GET("/api/metrics", c.GetClusterMetric)

  // GetClusterNodes - Get the nodes for a cluster
  e.GET("/api/nodes", c.GetClusterNodes)

  // GetClusterTables - Get list of DB tables per YB API (YCQL/YSQL)
  e.GET("/api/tables", c.GetClusterTables)

  // GetLiveQueries - Get the live queries in a cluster
  e.GET("/api/live_queries", c.GetLiveQueries)

  // GetSlowQueries - Get the slow queries in a cluster
  e.GET("/api/slow_queries", c.GetSlowQueries)

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
  e.Logger.Fatal(e.Start(port))
}
