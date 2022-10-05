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

  // BatchInviteAccountUser - Batch add or invite user to account
  e.POST("/api/public/accounts/:accountId/users/batch", c.BatchInviteAccountUser)

  // CreateAccount - Create an account
  e.POST("/api/public/accounts", c.CreateAccount)

  // DeleteAccount - Delete account
  e.DELETE("/api/public/accounts/:accountId", c.DeleteAccount)

  // GetAccount - Get account info
  e.GET("/api/public/accounts/:accountId", c.GetAccount)

  // GetAccountByName - Get account by name
  e.GET("/api/public/accounts", c.GetAccountByName)

  // GetAccountQuotas - Get account quotas
  e.GET("/api/public/accounts/:accountId/quotas", c.GetAccountQuotas)

  // GetAccountUser - Get user info
  e.GET("/api/public/accounts/:accountId/users/:userId", c.GetAccountUser)

  // GetAllowedLoginTypes - Get allowed login types for account
  e.GET("/api/public/accounts/:accountId/allowed_login_types", c.GetAllowedLoginTypes)

  // InviteAccountUser - Add or Invite user to account
  e.POST("/api/public/accounts/:accountId/users", c.InviteAccountUser)

  // ListUsers - List users
  e.GET("/api/public/accounts/:accountId/users", c.ListUsers)

  // ModifyAccount - Modify account
  e.PUT("/api/public/accounts/:accountId", c.ModifyAccount)

  // ModifyAllowedLoginTypes - Modify allowed login types for account
  e.PUT("/api/public/accounts/:accountId/allowed_login_types", c.ModifyAllowedLoginTypes)

  // ModifyUserRole - Modify user role
  e.PUT("/api/public/accounts/:accountId/users/:userId/roles", c.ModifyUserRole)

  // RemoveAccountUser - Remove user from account
  e.DELETE("/api/public/accounts/:accountId/users/:userId", c.RemoveAccountUser)

  // ListAlertNotifications - API to fetch the alert notifications for an account
  e.GET("/api/public/accounts/:accountId/alerts/notifications", c.ListAlertNotifications)

  // ListAlertRules - API to fetch the alert rules for an account
  e.GET("/api/public/accounts/:accountId/alert_rules", c.ListAlertRules)

  // SendTestEmailAlert - API to send test email alerts to users of an account
  e.POST("/api/public/accounts/:accountId/alert_rules/:alertRuleId/test_email",
    c.SendTestEmailAlert)

  // UpdateAlertRule - API to modify alert rule for an account
  e.PUT("/api/public/accounts/:accountId/alert_rules/:alertRuleId", c.UpdateAlertRule)

  // GetAuditEventById - Get detailed information about a specific audit log event
  e.GET("/api/public/accounts/:accountId/audit/events/:auditEventId", c.GetAuditEventById)

  // GetAuditEventCategories - Get audit event categories
  e.GET("/api/public/accounts/:accountId/audit/categories", c.GetAuditEventCategories)

  // ListAuditEvents - Get list of audit events for a given account
  e.GET("/api/public/accounts/:accountId/audit/events", c.ListAuditEvents)

  // CreateAuthToken - Create a new auth token
  e.POST("/api/public/auth/tokens", c.CreateAuthToken)

  // DeleteAuthToken - Delete auth token
  e.DELETE("/api/public/auth/tokens/:tokenId", c.DeleteAuthToken)

  // GetAuthToken - Get auth token
  e.GET("/api/public/auth/tokens/:tokenId", c.GetAuthToken)

  // ListAuthTokens - List auth tokens
  e.GET("/api/public/auth/tokens", c.ListAuthTokens)

  // ListRoles - List system defined RBAC roles
  e.GET("/api/public/auth/roles", c.ListRoles)

  // Login - Login a user
  e.POST("/api/public/auth/login", c.Login)

  // Logout - Logout a user
  e.POST("/api/public/auth/logout", c.Logout)

  // CreateBackup - Create backups
  e.POST("/api/public/accounts/:accountId/projects/:projectId/backups", c.CreateBackup)

  // DeleteBackup - Delete a backup
  e.DELETE("/api/public/accounts/:accountId/projects/:projectId/backups/:backupId",
    c.DeleteBackup)

  // DeleteClusterBackups - Submit task to delete all backups of a cluster
  e.DELETE("/api/public/accounts/:accountId/projects/:projectId/clusters/:clusterId/backups",
    c.DeleteClusterBackups)

  // DeleteSchedule - Delete a schedule
  e.DELETE("/api/public/accounts/:accountId/projects/:projectId/backup_schedules/:scheduleId",
    c.DeleteSchedule)

  // EditBackupSchedule - Edit the backup schedule
  e.PUT("/api/public/accounts/:accountId/projects/:projectId/backup_schedules/:scheduleId",
    c.EditBackupSchedule)

  // GetBackup - Get a backup
  e.GET("/api/public/accounts/:accountId/projects/:projectId/backups/:backupId", c.GetBackup)

  // GetSchedule - Get a schedule
  e.GET("/api/public/accounts/:accountId/projects/:projectId/backup_schedules/:scheduleId",
    c.GetSchedule)

  // ListBackups - List backups
  e.GET("/api/public/accounts/:accountId/projects/:projectId/backups", c.ListBackups)

  // ListRestores - List restore operations
  e.GET("/api/public/accounts/:accountId/projects/:projectId/restore", c.ListRestores)

  // ListSchedules - List schedules
  e.GET("/api/public/accounts/:accountId/projects/:projectId/backup_schedules",
    c.ListSchedules)

  // RestoreBackup - Restore a backup to a Cluster
  e.POST("/api/public/accounts/:accountId/projects/:projectId/restore", c.RestoreBackup)

  // ScheduleBackup - Schedule a backup
  e.POST("/api/public/accounts/:accountId/projects/:projectId/backup_schedules",
    c.ScheduleBackup)

  // AttachPaymentMethod - Attaches payment method to the stripe customer
  e.POST("/api/public/billing/accounts/:accountId/payment_methods/attach",
    c.AttachPaymentMethod)

  // CreateBillingProfile - This API adds billing profile
  e.POST("/api/public/billing/accounts/:accountId/billing_profile", c.CreateBillingProfile)

  // CreateSetupIntent - Create set up intent object
  e.POST("/api/public/billing/accounts/:accountId/set_up_intent/create", c.CreateSetupIntent)

  // DeletePaymentMethod - This API deletes payment method
  e.DELETE("/api/public/billing/accounts/:accountId/payment_methods/:paymentMethodId",
    c.DeletePaymentMethod)

  // EstimateClusterCost - This API to calculate the estimated cost of the cluster
  e.POST("/api/public/billing/accounts/:accountId/estimate", c.EstimateClusterCost)

  // GetBillingProfile - This API gets billing profile
  e.GET("/api/public/billing/accounts/:accountId/billing_profile", c.GetBillingProfile)

  // GetDefaultPaymentMethod - Get default payment method
  e.GET("/api/public/billing/accounts/:accountId/default_payment_method",
    c.GetDefaultPaymentMethod)

  // GetRateInfo - Get rate info of an account
  e.GET("/api/public/billing/accounts/:accountId/"+
    "projects/:projectId/clusters/:clusterId/rate_info", c.GetRateInfo)

  // ListCredits - Get list of credits for an account
  e.GET("/api/public/billing/accounts/:accountId/credits", c.ListCredits)

  // ListPaymentMethods - Lists billing payment methods
  e.GET("/api/public/billing/accounts/:accountId/payment_methods", c.ListPaymentMethods)

  // ModifyBillingProfile - This API updates billing profile
  e.PUT("/api/public/billing/accounts/:accountId/billing_profile", c.ModifyBillingProfile)

  // SetDefaultPaymentMethod - This API sets default payment method
  e.POST("/api/public/billing/accounts/:accountId/payment_methods/"+
    ":paymentMethodId/default_payment_method", c.SetDefaultPaymentMethod)

  // GetBillingInvoiceSummary - Billing invoice summary
  e.GET("/api/public/billing-invoice/accounts/:accountId/summary",
    c.GetBillingInvoiceSummary)

  // GetBillingInvoiceSummaryByInvoiceId - Billing invoice summary by invoice id
  e.GET("/api/public/billing-invoice/accounts/:accountId/invoices/:invoiceId/summary",
    c.GetBillingInvoiceSummaryByInvoiceId)

  // GetUsageSummary - Get account's summary usage
  e.GET("/api/public/billing-invoice/accounts/:accountId/invoices/:invoiceId/usage_summary",
    c.GetUsageSummary)

  // GetUsageSummaryStatistics - Get account's summary usage statistics
  e.GET("/api/public/billing-invoice/accounts/:accountId/invoices/"+
    ":invoiceId/usage_summary_statistics", c.GetUsageSummaryStatistics)

  // ListInvoices - Get list of invoices for an account
  e.GET("/api/public/billing/accounts/:accountId/invoices", c.ListInvoices)

  // CreateCluster - Create a cluster
  e.POST("/api/public/accounts/:accountId/projects/:projectId/clusters", c.CreateCluster)

  // DeleteCluster - Submit task to delete a cluster
  e.DELETE("/api/cluster", c.DeleteCluster)

  // EditCluster - Submit task to edit a cluster
  e.PUT("/api/cluster", c.EditCluster)

  // GetCluster - Get a cluster
  e.GET("/api/cluster", c.GetCluster)

  // ListClusters - List clusters
  e.GET("/api/public/accounts/:accountId/projects/:projectId/clusters", c.ListClusters)

  // NodeOp - Submit task to operate on a node
  e.POST("/api/public/accounts/:accountId/projects/:projectId/clusters/:clusterId/nodes/op",
    c.NodeOp)

  // PauseCluster - Submit task to pause a cluster
  e.POST("/api/public/accounts/:accountId/projects/:projectId/clusters/:clusterId/pause",
    c.PauseCluster)

  // ResumeCluster - Submit task to resume a cluster
  e.POST("/api/public/accounts/:accountId/projects/:projectId/clusters/:clusterId/resume",
    c.ResumeCluster)

  // GetBulkClusterMetrics - Get bulk cluster metrics
  e.GET("/api/public/accounts/:accountId/projects/:projectId/cluster_metrics",
    c.GetBulkClusterMetrics)

  // GetClusterMetric - Get a metric for a cluster
  e.GET("/api/metrics", c.GetClusterMetric)

  // GetClusterNodes - Get the nodes for a cluster
  e.GET("/api/nodes", c.GetClusterNodes)

  // GetClusterTables - Get list of DB tables per YB API (YCQL/YSQL)
  e.GET("/api/tables", c.GetClusterTables)

  // GetClusterTablespaces - Get list of DB tables for YSQL
  e.GET("/api/public/accounts/:accountId/projects/:projectId/"+
    "clusters/:clusterId/tablespaces", c.GetClusterTablespaces)

  // GetLiveQueries - Get the live queries in a cluster
  e.GET("/api/live_queries", c.GetLiveQueries)

  // GetSlowQueries - Get the slow queries in a cluster
  e.GET("/api/slow_queries", c.GetSlowQueries)

  // EditClusterNetworkAllowLists - Modify set of allow lists associated to a cluster
  e.PUT("/api/public/accounts/:accountId/projects/:projectId/"+
    "clusters/:clusterId/network/allow_lists", c.EditClusterNetworkAllowLists)

  // ListClusterNetworkAllowLists - Get list of allow list entities associated to a cluster
  e.GET("/api/public/accounts/:accountId/projects/:projectId/"+
    "clusters/:clusterId/network/allow_lists", c.ListClusterNetworkAllowLists)

  // GetCACert - Get certificate for connection to the cluster
  e.GET("/api/public/certificate", c.GetCACert)

  // GetClusterTierSpecs - Get base prices and specs of free and paid tier clusters
  e.GET("/api/public/clusters/tier_spec", c.GetClusterTierSpecs)

  // GetInstanceTypes - Get the list of supported
  // instance types for a given region/zone and provider
  e.GET("/api/public/:accountId/instance_types/:cloud", c.GetInstanceTypes)

  // GetRegions - Retrieve list of regions available to deploy cluster by cloud
  e.GET("/api/public/regions/:cloud", c.GetRegions)

  // GetPing - A simple ping healthcheck endpoint
  e.GET("/api/public/ping", c.GetPing)

  // ListAccounts - List accounts
  e.GET("/api/private/account", c.ListAccounts)

  // DeleteAdminApiToken - Delete admin token
  e.DELETE("/api/private/auth/admin_token/:adminTokenId", c.DeleteAdminApiToken)

  // GetAdminApiToken - Create an admin JWT for bearer authentication
  e.GET("/api/private/auth/admin_token", c.GetAdminApiToken)

  // ListAdminApiTokens - List admin JWTs
  e.GET("/api/private/auth/admin_token/list", c.ListAdminApiTokens)

  // GetBackupInfo - Get backup info along with the location
  e.GET("/api/private/accounts/:accountId/projects/"+
    ":projectId/backups/:backupId", c.GetBackupInfo)

  // RestoreMigrationBackup - Restore a backup from the specified bucket to a Cluster
  e.POST("/api/private/accounts/:accountId/projects/"+
    ":projectId/restore_migration", c.RestoreMigrationBackup)

  // AddCreditToBillingAccount - API to add credits to the given account
  e.POST("/api/private/billing/accounts/:accountId/credits", c.AddCreditToBillingAccount)

  // Aggregate - Run daily billing aggregation
  e.POST("/api/private/billing/accounts/:accountId/invoice/aggregate", c.Aggregate)

  // CreateRateCard - Creates rate card for the account
  e.POST("/api/private/billing/accounts/:accountId/rate_card", c.CreateRateCard)

  // DeleteInvoice - Delete billing invoice
  e.DELETE("/api/private/billing/accounts/:accountId/invoices/:invoiceId", c.DeleteInvoice)

  // GenerateInvoice - Generate an invoice for the account
  e.POST("/api/private/billing/accounts/:accountId/invoice/generate", c.GenerateInvoice)

  // SetAutomaticInvoiceGeneration - Enable or disable automatic invoice generation
  e.POST("/api/private/billing/invoice/set_automatic_invoice_generation",
    c.SetAutomaticInvoiceGeneration)

  // UpdateBillingInvoice - Update billing invoice
  e.PUT("/api/private/billing/accounts/:accountId/invoices/:invoiceId",
    c.UpdateBillingInvoice)

  // UpdateCreditsForAccount - API to update credits for the given account
  e.PUT("/api/private/billing/accounts/:accountId/credits/:creditId",
    c.UpdateCreditsForAccount)

  // UpdateGlobalRateCard - Updates global rate card
  e.PUT("/api/private/billing/global_rate_card", c.UpdateGlobalRateCard)

  // UpdatePaymentMethod - API to update billing method to OTHER/EMPLOYEE
  e.PUT("/api/private/billing/accounts/:accountId/payment_method", c.UpdatePaymentMethod)

  // CreatePrivateCluster - Create a Private cluster
  e.POST("/api/private/accounts/:accountId/projects/:projectId/clusters",
    c.CreatePrivateCluster)

  // DbUpgrade - Submit task to upgrade DB version of a cluster
  e.POST("/api/private/accounts/:accountId/projects/:projectId/"+
    "clusters/:clusterId/upgrade/db", c.DbUpgrade)

  // EditPrivateCluster - Submit task to edit a private cluster
  e.PUT("/api/private/accounts/:accountId/projects/:projectId/clusters/:clusterId",
    c.EditPrivateCluster)

  // GetClusterInternalDetails - Get a cluster
  e.GET("/api/private/accounts/:accountId/projects/:projectId/clusters/:clusterId",
    c.GetClusterInternalDetails)

  // GetDbReleases - Get all the available DB releases for upgrade
  e.GET("/api/private/db_releases", c.GetDbReleases)

  // GetPlatformForCluster - Get data of platform which manages the given cluster
  e.GET("/api/private/accounts/:accountId/projects/:projectId/clusters/:clusterId/platform",
    c.GetPlatformForCluster)

  // GflagsUpgrade - Submit task to upgrade gflags of a cluster
  e.POST("/api/private/accounts/:accountId/projects/:projectId/clusters/:clusterId/gflags",
    c.GflagsUpgrade)

  // ListGFlags - List all GFlags on a cluster
  e.GET("/api/private/accounts/:accountId/projects/:projectId/clusters/:clusterId/gflags",
    c.ListGFlags)

  // LockClusterForSupport - Acquire lock on the cluster
  e.POST("/api/private/accounts/:accountId/projects/:projectId/clusters/:clusterId/lock",
    c.LockClusterForSupport)

  // RebuildScrapeTargets - Rebuild prometheus configmap for scrape targets
  e.PUT("/api/private/clusters/scrape_targets", c.RebuildScrapeTargets)

  // UnlockClusterForSupport - Release lock on the cluster
  e.POST("/api/private/accounts/:accountId/projects/:projectId/clusters/:clusterId/unlock",
    c.UnlockClusterForSupport)

  // VmUpgrade - Submit task to upgrade VM image of a cluster
  e.POST("/api/private/accounts/:accountId/projects/:projectId/"+
    "clusters/:clusterId/upgrade/vm", c.VmUpgrade)

  // GetInternalClusterNodes - Get internal nodes for a cluster
  e.GET("/api/private/accounts/:accountId/projects/:projectId/clusters/:clusterId/nodes",
    c.GetInternalClusterNodes)

  // EditExternalOrInternalClusterNetworkAllowLists -
  // Modify set of allow lists associated to a cluster
  e.PUT("/api/private/accounts/:accountId/projects/:projectId/"+
    "clusters/:clusterId/network/allow_lists",
    c.EditExternalOrInternalClusterNetworkAllowLists)

  // ListAllClusterNetworkAllowLists - Get list of allow list entities associated to a cluster
  e.GET("/api/private/accounts/:accountId/projects/:projectId/"+
    "clusters/:clusterId/network/allow_lists", c.ListAllClusterNetworkAllowLists)

  // AddCustomImageToSet - API to add a custom image to the specified custom image set
  e.POST("/api/private/custom_image_sets/:customImageSetId", c.AddCustomImageToSet)

  // CreateCustomImageSetsInBulk - API to create custom image sets in bulk
  e.POST("/api/private/custom_image_sets", c.CreateCustomImageSetsInBulk)

  // DeleteCustomImageSet - Delete custom image set
  e.DELETE("/api/private/custom_image_sets/:customImageSetId", c.DeleteCustomImageSet)

  // GetCustomImageSetDetails - API to get details about custom image set
  e.GET("/api/private/custom_image_sets/:customImageSetId", c.GetCustomImageSetDetails)

  // ListCustomImageSets - API to list custom image sets
  e.GET("/api/private/custom_image_sets", c.ListCustomImageSets)

  // MarkCustomImageSetAsDefault - Mark a custom image set as default
  e.POST("/api/private/custom_image_sets/:customImageSetId/default",
    c.MarkCustomImageSetAsDefault)

  // SendEmail - Send email with given template
  e.POST("/api/private/email", c.SendEmail)

  // ArmFaultInjectionForEntity - Arm fault injection
  e.POST("/api/private/fault_injection/arm", c.ArmFaultInjectionForEntity)

  // DisarmFaultInjectionForEntity - Disarm fault injection
  e.DELETE("/api/private/fault_injection/disarm", c.DisarmFaultInjectionForEntity)

  // GetEntityRefs - Get list of entity refs for the specified fault
  e.GET("/api/private/fault_injection/:fault_name", c.GetEntityRefs)

  // GetFaultNames - Get fault injections
  e.GET("/api/private/fault_injection", c.GetFaultNames)

  // GetLoggingLevel - Get Logging Level
  e.GET("/api/private/logger", c.GetLoggingLevel)

  // SetLoggingLevel - Set Logging Level
  e.PUT("/api/private/logger", c.SetLoggingLevel)

  // UpdateGflagMaintenance - API to set use_custom_gflags flag
  // for a cluster's maintenance schedule
  e.POST("/api/private/accounts/:accountId/projects/"+
    ":projectId/clusters/:clusterId/maintenance/schedule/gflags",
    c.UpdateGflagMaintenance)

  // GetVersion - Get application version
  e.GET("/api/private/version", c.GetVersion)

  // GetMeteringData - Get metering data
  e.GET("/api/private/metering/accounts/:accountId", c.GetMeteringData)

  // AddNetwork - Add new cluster network
  e.POST("/api/private/network", c.AddNetwork)

  // CreateInternalNetworkAllowList - Create a private allow list entity
  e.POST("/api/private/accounts/:accountId/projects/:projectId/network/allow_lists",
    c.CreateInternalNetworkAllowList)

  // CreateInternalVpcPeering - Peer two yugabyte VPC
  e.POST("/api/private/accounts/:accountId/projects/:projectId/network/vpcs/:vpcId/peer",
    c.CreateInternalVpcPeering)

  // CreateSingleTenantVpcMetadata - Create customer-facing VPC metadata for cluster isolation
  e.POST("/api/private/accounts/:accountId/projects/:projectId/network/vpcs",
    c.CreateSingleTenantVpcMetadata)

  // DeleteExternalOrInternalNetworkAllowList - Delete an allow list entity
  e.DELETE("/api/private/accounts/:accountId/projects/:projectId/"+
    "network/allow_lists/:allowListId",
    c.DeleteExternalOrInternalNetworkAllowList)

  // DeleteInternalVpcPeering - Delete internal VPC peering between two yugabyte VPC
  e.DELETE("/api/private/accounts/:accountId/projects/:projectId/network/vpcs/:peeringId",
    c.DeleteInternalVpcPeering)

  // GetExternalOrInternalNetworkAllowList - Retrieve an allow list entity
  e.GET("/api/private/accounts/:accountId/projects/:projectId/"+
    "network/allow_lists/:allowListId", c.GetExternalOrInternalNetworkAllowList)

  // GetVpc - Get network info by ID
  e.GET("/api/private/network/:vpcId", c.GetVpc)

  // ListAllNetworkAllowLists - Get list of allow list entities
  e.GET("/api/private/accounts/:accountId/projects/:projectId/network/allow_lists",
    c.ListAllNetworkAllowLists)

  // ListNetworks - List all cluster networks
  e.GET("/api/private/network", c.ListNetworks)

  // MarkVpcsForMaintenance - Mark VPCs for Maintenance
  e.POST("/api/private/network/maintenance", c.MarkVpcsForMaintenance)

  // AddPlatform - Add new platform
  e.POST("/api/private/platform", c.AddPlatform)

  // AddProjectToPlatform - Add project to platform
  e.POST("/api/private/platform/:platformId/project", c.AddProjectToPlatform)

  // GetPlatform - Get platform by ID
  e.GET("/api/private/platform/:platformId", c.GetPlatform)

  // ListPlatforms - List platforms
  e.GET("/api/private/platform", c.ListPlatforms)

  // MarkPlatformsForMaintenance - Mark Platforms for Maintenance
  e.POST("/api/private/platform/maintenance", c.MarkPlatformsForMaintenance)

  // RefreshProviderPricing - Refresh pricing in specified existing customer providers
  e.PUT("/api/private/platform/providers/refresh-pricing", c.RefreshProviderPricing)

  // GetRuntimeConfig - Get runtime configuration
  e.GET("/api/private/runtime_config", c.GetRuntimeConfig)

  // UpdateRuntimeConfig - Update configuration keys for given scope.
  e.PUT("/api/private/runtime_config", c.UpdateRuntimeConfig)

  // CancelScheduledUpgrade - Cancel a scheduled upgrade task
  e.DELETE("/api/private/scheduled_upgrade/:taskId", c.CancelScheduledUpgrade)

  // ListScheduledUpgrades - List currently scheduled upgrade tasks
  e.GET("/api/private/scheduled_upgrade", c.ListScheduledUpgrades)

  // RemoveClusterFromExecution - Remove a cluster from a scheduled upgrade execution
  e.DELETE("/api/private/scheduled_upgrade/:taskId/clusters/:clusterId",
    c.RemoveClusterFromExecution)

  // ScheduleBulkUpgrade - Schedule an upgrade based on
  // cluster tier and optionally cloud/region
  e.POST("/api/private/scheduled_upgrade/tracks/:trackId", c.ScheduleBulkUpgrade)

  // ScheduleClusterUpgrade - Schedule an Upgrade for the specified Cluster
  e.POST("/api/private/scheduled_upgrade/accounts/:accountId/"+
    "projects/:projectId/clusters/:clusterId", c.ScheduleClusterUpgrade)

  // BatchAddTracks - Add release tracks to account
  e.POST("/api/private/accounts/:accountId/software/tracks", c.BatchAddTracks)

  // CreateRelease - Create a software release
  e.POST("/api/private/software/tracks/:trackId/releases", c.CreateRelease)

  // CreateTrack - Create a DB software release track
  e.POST("/api/private/software/tracks", c.CreateTrack)

  // DeleteRelease - Delete a software release
  e.DELETE("/api/private/software/tracks/:trackId/releases/:releaseId", c.DeleteRelease)

  // DeleteTrack - Delete a DB software release track
  e.DELETE("/api/private/software/tracks/:trackId", c.DeleteTrack)

  // ListReleasesOnTrack - List all DB software releases by track
  e.GET("/api/private/software/tracks/:trackId/releases", c.ListReleasesOnTrack)

  // ListTracks - List all DB software release tracks
  e.GET("/api/private/software/tracks", c.ListTracks)

  // RemoveTrack - Remove release track from account
  e.DELETE("/api/private/accounts/:accountId/software/tracks/:trackId", c.RemoveTrack)

  // UpdateRelease - Update a software release
  e.PUT("/api/private/software/tracks/:trackId/releases/:releaseId", c.UpdateRelease)

  // GetAllowedValuesForInternalTags - API to fetch allowed values for internal tags
  e.GET("/api/private/internal_tags/allowed_values", c.GetAllowedValuesForInternalTags)

  // GetUserInternalTags - API to get user internal tags for a given user
  e.GET("/api/private/users/:userId/internal_tags", c.GetUserInternalTags)

  // ListAllDefaultInternalTags - API to fetch all the default internal tags
  e.GET("/api/private/internal_tags/default", c.ListAllDefaultInternalTags)

  // UpdateDefaultInternalTags - API to batch set/update default internal tags
  e.POST("/api/private/internal_tags/default", c.UpdateDefaultInternalTags)

  // UpdateUserInternalTags - API to set/update internal tags for a given user
  e.POST("/api/private/users/:userId/internal_tags", c.UpdateUserInternalTags)

  // ListTasksAll - List tasks
  e.GET("/api/private/accounts/:accountId/tasks", c.ListTasksAll)

  // RunScheduledTask - Run scheduled task
  e.POST("/api/private/scheduled_tasks/:task", c.RunScheduledTask)

  // EventCallback - Post a task-related event callback
  e.POST("/api/private/taskEvents/:eventId", c.EventCallback)

  // ActivateInvitedUserWithoutToken - Activate invited user by skipping token validation
  e.POST("/api/private/users/activate_invited", c.ActivateInvitedUserWithoutToken)

  // ActivateSignupUserWithoutToken - Activate signup user by skipping token validation
  e.POST("/api/private/users/activate", c.ActivateSignupUserWithoutToken)

  // CleanupUser - Delete user and remove the accounts/projects
  // of which they are the sole admin
  e.DELETE("/api/private/users/:userId/cleanup", c.CleanupUser)

  // ListAllUsers - List all users
  e.GET("/api/private/users", c.ListAllUsers)

  // CreateXclusterReplication - API to create replication
  e.POST("/api/private/accounts/:accountId/projects/:projectId/xcluster_replication",
    c.CreateXclusterReplication)

  // DelayMaintenanceEvent - API to delay maintenance events for a cluster
  e.POST("/api/public/accounts/:accountId/projects/:projectId/clusters/"+
    ":clusterId/maintenance/events/:executionId/delay", c.DelayMaintenanceEvent)

  // GetMaintenanceSchedule - API to get maintenance schedules
  e.GET("/api/public/accounts/:accountId/projects/:projectId/clusters/"+
    ":clusterId/maintenance/schedule", c.GetMaintenanceSchedule)

  // GetNextMaintenanceWindowInfo - API to get next maintenance window for a cluster
  e.GET("/api/public/accounts/:accountId/projects/:projectId/clusters/"+
    ":clusterId/maintenance/:executionId/next_available_window",
    c.GetNextMaintenanceWindowInfo)

  // ListScheduledMaintenanceEventsForCluster - API to list all scheduled
  // maintenance events for a cluster
  e.GET("/api/public/accounts/:accountId/projects/:projectId/clusters/"+
    ":clusterId/maintenance/events", c.ListScheduledMaintenanceEventsForCluster)

  // TriggerMaintenanceEvent - API to trigger maintenance events for a cluster
  e.POST("/api/public/accounts/:accountId/projects/:projectId/clusters/"+
    ":clusterId/maintenance/events/:executionId/trigger", c.TriggerMaintenanceEvent)

  // UpdateMaintenanceSchedule - API to update maintenance schedules
  e.PUT("/api/public/accounts/:accountId/projects/:projectId/"+
    "clusters/:clusterId/maintenance/schedule", c.UpdateMaintenanceSchedule)

  // CreateNetworkAllowList - Create an allow list entity
  e.POST("/api/public/accounts/:accountId/projects/:projectId/network/allow_lists",
    c.CreateNetworkAllowList)

  // CreateVpc - Create a dedicated VPC for your DB clusters
  e.POST("/api/public/accounts/:accountId/projects/:projectId/network/vpcs",
    c.CreateVpc)

  // CreateVpcPeering - Create a peering between customer VPC and Yugabyte VPC
  e.POST("/api/public/accounts/:accountId/projects/:projectId/network/vpc-peerings",
    c.CreateVpcPeering)

  // DeleteNetworkAllowList - Delete an allow list entity
  e.DELETE("/api/public/accounts/:accountId/projects/"+
    ":projectId/network/allow_lists/:allowListId",
    c.DeleteNetworkAllowList)

  // DeleteVpc - Delete customer-facing VPC by ID
  e.DELETE("/api/public/accounts/:accountId/projects/:projectId/network/vpcs/:vpcId",
    c.DeleteVpc)

  // DeleteVpcPeering - Delete VPC Peering
  e.DELETE("/api/public/accounts/:accountId/projects/:projectId/"+
    "network/vpc-peerings/:peeringId", c.DeleteVpcPeering)

  // GetNetworkAllowList - Retrieve an allow list entity
  e.GET("/api/public/accounts/:accountId/projects/:projectId/"+
    "network/allow_lists/:allowListId", c.GetNetworkAllowList)

  // GetSingleTenantVpc - Get customer-facing VPC by ID
  e.GET("/api/public/accounts/:accountId/projects/:projectId/network/vpcs/:vpcId",
    c.GetSingleTenantVpc)

  // GetVpcPeering - Get a VPC Peering
  e.GET("/api/public/accounts/:accountId/projects/:projectId/"+
    "network/vpc-peerings/:peeringId", c.GetVpcPeering)

  // ListNetworkAllowLists - Get list of allow list entities
  e.GET("/api/public/accounts/:accountId/projects/:projectId/network/allow_lists",
    c.ListNetworkAllowLists)

  // ListSingleTenantVpcs - Get customer-facing VPCs to choose for cluster isolation
  e.GET("/api/public/accounts/:accountId/projects/:projectId/network/vpcs",
    c.ListSingleTenantVpcs)

  // ListVpcPeerings - List peerings between customer VPCs and Yugabyte VPCs
  e.GET("/api/public/accounts/:accountId/projects/:projectId/network/vpc-peerings",
    c.ListVpcPeerings)

  // GetRestrictedCidrs - Get list of unavailable CIDRs
  e.GET("/api/public/networks/:cloud/restricted", c.GetRestrictedCidrs)

  // CreateProject - Create a project
  e.POST("/api/public/accounts/:accountId/projects", c.CreateProject)

  // DeleteProject - Delete project
  e.DELETE("/api/public/accounts/:accountId/projects/:projectId", c.DeleteProject)

  // GetProject - Get project info
  e.GET("/api/public/accounts/:accountId/projects/:projectId", c.GetProject)

  // ListProjects - List projects
  e.GET("/api/public/accounts/:accountId/projects", c.ListProjects)

  // CreateReadReplica - Create Read Replica
  e.POST("/api/public/accounts/:accountId/projects/:projectId/"+
    "clusters/:clusterId/read_replicas", c.CreateReadReplica)

  // GetReadReplica - Get Read Replicas
  e.GET("/api/public/accounts/:accountId/projects/:projectId"+
    "/clusters/:clusterId/read_replica/:readReplicaId", c.GetReadReplica)

  // ListReadReplicas - List Read Replicas
  e.GET("/api/public/accounts/:accountId/projects/:projectId/"+
    "clusters/:clusterId/read_replicas", c.ListReadReplicas)

  // GetRelease - Get Software Release by Id
  e.GET("/api/public/accounts/:accountId/software/tracks/:trackId/releases/:releaseId",
    c.GetRelease)

  // GetTrackById - Get release track by ID
  e.GET("/api/public/accounts/:accountId/software/tracks/:trackId", c.GetTrackById)

  // ListReleases - List DB software releases by track
  e.GET("/api/public/accounts/:accountId/software/tracks/:trackId/releases", c.ListReleases)

  // ListTracksForAccount - List all release tracks linked to account
  e.GET("/api/public/accounts/:accountId/software/tracks", c.ListTracksForAccount)

  // ListTasks - List tasks
  e.GET("/api/public/accounts/:accountId/tasks", c.ListTasks)

  // GetAppConfig - Get application configuration
  e.GET("/api/public/ui/app_config", c.GetAppConfig)

  // GetSsoRedirectUrl - Retrieve redirect URL for
  // Single Sign On using external authentication.
  e.GET("/api/public/ui/sso_redirect_url", c.GetSsoRedirectUrl)

  // GetUserTutorials - Get tutorials for a user
  e.GET("/api/public/ui/accounts/:accountId/users/:userId/tutorials", c.GetUserTutorials)

  // SsoInviteCallback - Callback for SSO invite
  e.GET("/api/public/ui/callback/sso_invite", c.SsoInviteCallback)

  // SsoLoginCallback - Callback for SSO login
  e.GET("/api/public/ui/callback/sso_login", c.SsoLoginCallback)

  // SsoSignupCallback - Callback for SSO signup
  e.GET("/api/public/ui/callback/sso_signup", c.SsoSignupCallback)

  // UpdateUserTutorial - Update tutorial for a user
  e.PUT("/api/public/ui/accounts/:accountId/users/:userId/tutorials/:tutorialId",
    c.UpdateUserTutorial)

  // UpdateUserTutorialEnabled - Update whether tutorial is enabled for a user
  e.PUT("/api/public/ui/accounts/:accountId/users/:userId/tutorials/:tutorialId/enabled",
    c.UpdateUserTutorialEnabled)

  // UpdateUserTutorialState - Update tutorial state status for a user
  e.PUT("/api/public/ui/accounts/:accountId/users/:userId/tutorials/"+
    ":tutorialId/state/:stateId/:is_completed", c.UpdateUserTutorialState)

  // ChangePassword - Change user password
  e.PUT("/api/public/users/self/password", c.ChangePassword)

  // CreateUser - Create a user
  e.POST("/api/public/users", c.CreateUser)

  // DeleteUser - Delete user
  e.DELETE("/api/public/users/self", c.DeleteUser)

  // GetUser - Get user info
  e.GET("/api/public/users/self", c.GetUser)

  // ListUserAccounts - Get account information for the user
  e.GET("/api/public/users/self/accounts", c.ListUserAccounts)

  // ModifyUser - Modify user info
  e.PUT("/api/public/users/self", c.ModifyUser)

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
