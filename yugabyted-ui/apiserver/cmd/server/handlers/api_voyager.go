package handlers

import (
    "apiserver/cmd/server/helpers"
    "apiserver/cmd/server/logger"
    "apiserver/cmd/server/models"
    "context"
    "encoding/json"
    "fmt"
    "math"
    "net/http"
    "os"
    "path/filepath"
    "runtime"
    "strconv"
    "strings"
    "time"

    "github.com/jackc/pgtype"
    "github.com/jackc/pgx/v4"
    "github.com/jackc/pgx/v4/pgxpool"
    "github.com/labstack/echo/v4"
)

const LOGGER_FILE_NAME = "api_voyager"
const MIGRATION_CAVEATS_UI_DISPLAY_STR ="MIGRATION CAVEATS"

// Gets one row for each unique migration_uuid, and also gets the highest migration_phase and
// invocation for import/export
// Need to ignore rows with migration_phase 3 i.e. schema analysis
const RETRIEVE_ALL_VOYAGER_MIGRATIONS_SQL string = 
`SELECT * FROM (

    SELECT migration_uuid, migration_phase AS highest_export_phase,
            MAX(invocation_sequence) AS highest_export_invocation
    FROM ybvoyager_visualizer.ybvoyager_visualizer_metadata
    WHERE (migration_uuid, migration_phase) IN (
            SELECT migration_uuid, MAX(migration_phase) AS highest_export_phase
            FROM ybvoyager_visualizer.ybvoyager_visualizer_metadata
            WHERE migration_phase IN (4,2,1) GROUP BY migration_uuid
    ) GROUP BY migration_uuid, migration_phase

) AS export FULL JOIN (

    SELECT migration_uuid, migration_phase AS highest_import_phase,
            MAX(invocation_sequence) AS highest_import_invocation
    FROM ybvoyager_visualizer.ybvoyager_visualizer_metadata
    WHERE (migration_uuid, migration_phase) IN (
            SELECT migration_uuid, MAX(migration_phase) AS highest_export_phase
            FROM ybvoyager_visualizer.ybvoyager_visualizer_metadata
            WHERE migration_phase > 4 AND migration_phase <=6 GROUP BY migration_uuid
    ) GROUP BY migration_uuid, migration_phase

) AS import USING (migration_uuid) FULL JOIN (

    SELECT migration_uuid, migration_phase AS lowest_phase,
            MIN(invocation_sequence) AS lowest_invocation
    FROM ybvoyager_visualizer.ybvoyager_visualizer_metadata
    WHERE (migration_uuid, migration_phase) IN (
            SELECT migration_uuid, MIN(migration_phase) AS lowest_phase
            FROM ybvoyager_visualizer.ybvoyager_visualizer_metadata
            GROUP BY migration_uuid
    ) GROUP BY migration_uuid, migration_phase

) AS lowest USING (migration_uuid)`

const RETRIEVE_VOYAGER_MIGRATION_DETAILS string = "SELECT database_name, schema_name, " +
    "status, invocation_timestamp, complexity, db_type, " +
    "migration_dir, host_ip, port, db_version, voyager_info " +
    "FROM ybvoyager_visualizer.ybvoyager_visualizer_metadata " +
    "WHERE migration_uuid=$1 AND migration_phase=$2 AND invocation_sequence=$3"

const RETRIEVE_VOYAGER_MIGRATION_START_TIMESTAMP string = "SELECT invocation_timestamp " +
    "FROM ybvoyager_visualizer.ybvoyager_visualizer_metadata " +
    "WHERE migration_uuid=$1 AND migration_phase=$2 AND invocation_sequence=$3"

const RETRIEVE_ANALYZE_SCHEMA_PAYLOAD string = "SELECT payload " +
    "FROM ybvoyager_visualizer.ybvoyager_visualizer_metadata " +
    "WHERE migration_uuid=$1 AND migration_phase=$2 AND invocation_sequence=$3"

const RETRIEVE_MIGRATE_SCHEMA_PHASES_INFO string = "SELECT migration_UUID, " +
    "migration_phase, MAX(invocation_sequence) AS invocation_sequence " +
    "FROM ybvoyager_visualizer.ybvoyager_visualizer_metadata " +
    "WHERE migration_uuid=$1 AND migration_phase IN (2,3,5) " +
    "GROUP BY migration_uuid, migration_phase;"

const RETRIEVE_DATA_MIGRATION_METRICS string = "SELECT * FROM " +
    "ybvoyager_visualizer.ybvoyager_visualizer_table_metrics " +
    "WHERE migration_UUID=$1 " +
    "ORDER BY schema_name"

const RETRIEVE_ASSESSMENT_REPORT string = "SELECT payload " +
    "FROM ybvoyager_visualizer.ybvoyager_visualizer_metadata " +
    "WHERE migration_UUID=$1 AND migration_phase=1 AND status='COMPLETED'"

const RETRIEVE_IMPORT_SCHEMA_STATUS string = "SELECT status " +
    "FROM ybvoyager_visualizer.ybvoyager_visualizer_metadata " +
    "WHERE migration_UUID=$1 AND migration_phase=$2 AND invocation_sequence=$3"

type VoyagerMigrationsQueryFuture struct {
    Migrations []models.VoyagerMigrationDetails
    Error      error
}

type AssessmentReportQueryFuture struct {
    Report helpers.AssessmentVisualisationMetadata
    Error error
}

type VoyagerDataMigrationMetricsFuture struct {
    Metrics []models.VoyagerMigrateDataMetricsDetails
    Error   error
}

type MigrateSchemaInfoFuture struct {
    Data  models.MigrateSchemaTaskInfo
    Error error
}

type MigrateSchemaPhasesInfo struct {
    migrationUuid  string
    migrationPhase int
    invocationSeq  int
}

type MigrateSchemaUIDetailFuture struct {
    SuggestionsErrors []models.ErrorsAndSuggestionsDetails
    SqlObjects        []models.SqlObjectsDetails
    Error             error
}

type PayloadReport struct {
    Summary SchemaAnalyzeReport                  `json:"summary"`
    Issues  []models.ErrorsAndSuggestionsDetails `json:"issues"`
}

type SchemaAnalyzeReport struct {
    DbName          string                     `json:"dbName"`
    SchemaName      string                     `json:"schemaName"`
    DbVersion       string                     `json:"dbVersion"`
    Notes           string                     `json:"notes"`
    DatabaseObjects []models.SqlObjectsDetails `json:"databaseObjects"`
}

type AllVoyagerMigrations struct {
    migrationUuid        string
    exportMigrationPhase pgtype.Int4
    exportInvocationSeq  pgtype.Int4
    importMigrationPhase pgtype.Int4
    importInvocationSeq  pgtype.Int4
    lowestMigrationPhase pgtype.Int4
    lowestInvocationSeq  pgtype.Int4
}

type VoyagerInfo struct {
    Ip        string `json:"IP"`
    Os        string `json:"OperatingSystem"`
    AvailDisk uint64 `json:"DiskSpaceAvailable"`
    ExportDir string `json:"ExportDirectory"`
}

type DbIp struct {
    SourceDbIp string `json:"SourceDBIP"`
    TargetDbIp string `json:"TargetDBIP"`
}

var MigrationPhaseStrings = []string{
    "Assessment",
    "Export Schema",
    "Analyze Schema",
    "Export Data",
    "Import Schema",
    "Import Data",
    "Verify",      
}

func (c *Container) GetVoyagerMigrations(ctx echo.Context) error {

    voyagerMigrationsResponse := models.VoyagerMigrationsInfo{
        Migrations: []models.VoyagerMigrationDetails{},
    }

    conn, err := c.GetConnection("yugabyte")
    if err != nil {
        return ctx.String(http.StatusInternalServerError, err.Error())
    }
    future := make(chan VoyagerMigrationsQueryFuture)
    go getVoyagerMigrationsQueryFuture(c.logger, conn, future)

    voyagerMigrations := <-future
    if voyagerMigrations.Error != nil {
        return ctx.JSON(http.StatusOK, voyagerMigrationsResponse)
    }
    voyagerMigrationsResponse.Migrations = voyagerMigrations.Migrations
    return ctx.JSON(http.StatusOK, voyagerMigrationsResponse)
}

func (c *Container) GetVoyagerMetrics(ctx echo.Context) error {

    VoyagerMigrationMetricsRespone := models.VoyagerMigrateDataMetrics{
        Metrics: []models.VoyagerMigrateDataMetricsDetails{},
    }
    var migrationUuid string
    migrationUuid = ctx.QueryParam("uuid")

    conn, err := c.GetConnection("yugabyte")
    if err != nil {
        return ctx.String(http.StatusInternalServerError, err.Error())
    }

    future := make(chan VoyagerDataMigrationMetricsFuture)
    go getVoyagerDataMigrationMetricsFuture(c.logger, conn, migrationUuid, future)
    DataMigrationMetrics := <-future
    if DataMigrationMetrics.Error != nil {
        return ctx.String(http.StatusInternalServerError,
            DataMigrationMetrics.Error.Error())
    }
    VoyagerMigrationMetricsRespone.Metrics = DataMigrationMetrics.Metrics
    return ctx.JSON(http.StatusOK, VoyagerMigrationMetricsRespone)
}

func (c *Container) GetMigrateSchemaInfo(ctx echo.Context) error {

    migrateSchemaTaskInfoResponse := models.MigrateSchemaTaskInfo{}
    migrationUuid := ctx.QueryParam("uuid")

    conn, err := c.GetConnection("yugabyte")
    if err != nil {
        return ctx.String(http.StatusInternalServerError, err.Error())
    }
    future := make(chan MigrateSchemaInfoFuture)
    go getMigrateSchemaTaskInfoFuture(c.logger, conn, migrationUuid, future)
    MigrateSchemaInfo := <-future
    if MigrateSchemaInfo.Error != nil {
        return ctx.String(http.StatusInternalServerError,
            MigrateSchemaInfo.Error.Error())
    }
    migrateSchemaTaskInfoResponse = MigrateSchemaInfo.Data
    return ctx.JSON(http.StatusOK, migrateSchemaTaskInfoResponse)
}

func getVoyagerMigrationsQueryFuture(log logger.Logger, conn *pgxpool.Pool,
    future chan VoyagerMigrationsQueryFuture) {

    voyagerMigrationsResponse := VoyagerMigrationsQueryFuture{
        Migrations: []models.VoyagerMigrationDetails{},
        Error:      nil,
    }
    
    rows, err := conn.Query(context.Background(), RETRIEVE_ALL_VOYAGER_MIGRATIONS_SQL)
    if err != nil {
        log.Errorf(fmt.Sprintf("[%s] Error while executing query: [%s]",
            LOGGER_FILE_NAME, "RETRIEVE_ALL_VOYAGER_MIGRATIONS_SQL"))
        log.Errorf(err.Error())
        voyagerMigrationsResponse.Error = err
        future <- voyagerMigrationsResponse
        return
    }
    defer rows.Close()

    var allVoyagerMigrationsList []AllVoyagerMigrations
    for rows.Next() {
        allVoyagerMigrationsStruct := AllVoyagerMigrations{}
        err := rows.Scan(&allVoyagerMigrationsStruct.migrationUuid,
            &allVoyagerMigrationsStruct.exportMigrationPhase,
            &allVoyagerMigrationsStruct.exportInvocationSeq,
            &allVoyagerMigrationsStruct.importMigrationPhase,
            &allVoyagerMigrationsStruct.importInvocationSeq,
            &allVoyagerMigrationsStruct.lowestMigrationPhase,
            &allVoyagerMigrationsStruct.lowestInvocationSeq,
        )
        if err != nil {
            log.Errorf(fmt.Sprintf("[%s] Error while scaning results for query: [%s]",
                LOGGER_FILE_NAME, "RETRIEVE_ALL_VOYAGER_MIGRATIONS_SQL"))
            log.Errorf(err.Error())
            continue
        }
        allVoyagerMigrationsList = append(allVoyagerMigrationsList, allVoyagerMigrationsStruct)
    }

    for _, allVoyagerMigration := range allVoyagerMigrationsList {

        assessmentFuture := make(chan AssessmentReportQueryFuture)
        go getMigrationAssessmentReportFuture(log, allVoyagerMigration.migrationUuid, conn,
            assessmentFuture)

        // Update migration data struct using most recent import and export step.
        // This is done since source db info is only in export rows, target db info in import rows.
        // Info from import rows will overwrite info from export rows.
        migrationDetailsStruct := models.VoyagerMigrationDetails{}
        if allVoyagerMigration.exportMigrationPhase.Status == pgtype.Present &&
            allVoyagerMigration.exportInvocationSeq.Status == pgtype.Present {
            err := updateMigrationDetailStruct(log, conn, &migrationDetailsStruct,
                allVoyagerMigration.migrationUuid,
                allVoyagerMigration.exportMigrationPhase.Int,
                allVoyagerMigration.exportInvocationSeq.Int)
            if err != nil {
                log.Errorf("[%s] Error while querying for export phase migration details",
                    LOGGER_FILE_NAME)
            }
        }
        if allVoyagerMigration.importMigrationPhase.Status == pgtype.Present &&
            allVoyagerMigration.importInvocationSeq.Status == pgtype.Present {
            err := updateMigrationDetailStruct(log, conn, &migrationDetailsStruct,
                allVoyagerMigration.migrationUuid,
                allVoyagerMigration.importMigrationPhase.Int,
                allVoyagerMigration.importInvocationSeq.Int)
            if err != nil {
                log.Errorf("[%s] Error while querying for import phase migration details",
                    LOGGER_FILE_NAME)
            }
        }
        if allVoyagerMigration.lowestMigrationPhase.Status == pgtype.Present &&
            allVoyagerMigration.lowestInvocationSeq.Status == pgtype.Present {
            err := updateMigrationDetailStructStartTimestamp(log, conn, &migrationDetailsStruct,
                allVoyagerMigration.migrationUuid,
                allVoyagerMigration.lowestMigrationPhase.Int,
                allVoyagerMigration.lowestInvocationSeq.Int)
            if err != nil {
                log.Errorf("[%s] Error while querying for migration start timestamp",
                    LOGGER_FILE_NAME)
            }
        }

        // Get complexity from assessment if it exists.
        migrationDetailsStruct.Complexity = "N/A"
        assessment := <-assessmentFuture
        if assessment.Error != nil {
            log.Errorf("[%s] Error getting migration assessment",
                LOGGER_FILE_NAME)
        } else {
            migrationDetailsStruct.Complexity = assessment.Report.MigrationComplexity
        }
        voyagerMigrationsResponse.Migrations = append(
            voyagerMigrationsResponse.Migrations, migrationDetailsStruct)
    }
    future <- voyagerMigrationsResponse
}

func updateMigrationDetailStruct(log logger.Logger, conn *pgxpool.Pool,
    migrationDetailsStruct *models.VoyagerMigrationDetails, migrationUuid string,
    migrationPhase int32, invocationSeq int32) error {

    voyagerDetailsrows, err := conn.Query(context.Background(),
        RETRIEVE_VOYAGER_MIGRATION_DETAILS, migrationUuid, migrationPhase, invocationSeq)
    if err != nil {
        log.Errorf(fmt.Sprintf("[%s] Error while querying for migration details",
            LOGGER_FILE_NAME))
        log.Errorf(err.Error())
        return err
    }

    for voyagerDetailsrows.Next() {
        var database pgtype.Text
        var schema pgtype.Text
        var status pgtype.Text
        var exportDir pgtype.Text
        var invocation_ts time.Time
        var complexity pgtype.Text
        var voyagerInfo pgtype.Text
        var dbIp pgtype.Text
        var dbPort pgtype.Int4
        var dbType pgtype.Text
        var dbVersion pgtype.Text

        migrationDetailsStruct.MigrationUuid = migrationUuid
        migrationDetailsStruct.MigrationPhase = migrationPhase
        migrationDetailsStruct.InvocationSequence = invocationSeq
        err = voyagerDetailsrows.Scan(&database, &schema,
            &status, &invocation_ts, &complexity, &dbType,
            &exportDir, &dbIp, &dbPort,
            &dbVersion, &voyagerInfo)
        if err != nil {
            log.Errorf(fmt.Sprintf("[%s] Error while scanning migration details",
                LOGGER_FILE_NAME))
            log.Errorf(err.Error())
            continue
        }

        if database.Status == pgtype.Present && database.String != "" {
            migrationDetailsStruct.SourceDb.Database = database.String
        }
        if schema.Status == pgtype.Present && schema.String != "" {
            migrationDetailsStruct.SourceDb.Schema = schema.String
        }
        if status.Status == pgtype.Present && status.String != "" {
            migrationDetailsStruct.Status = status.String
        }
        if exportDir.Status == pgtype.Present && exportDir.String != "" {
            migrationDetailsStruct.Voyager.ExportDir = exportDir.String
        }

        // Use rowStruct.Status, fall back to invocationSeq, assume >= 2 means completed.
        isCompleted := false
        if migrationDetailsStruct.Status == "" {
            isCompleted = invocationSeq >= 2
        } else {
            isCompleted = migrationDetailsStruct.Status == "COMPLETED"
        }

        // If migration assessment is completed => Assessment
        // If schema migration not completed => Schema migration
        // If data migration not completed => Data migration
        // Otherwise migration is complete => Completed
        if migrationPhase <= 1 {
            migrationDetailsStruct.Progress = "Assessment"
        } else if migrationPhase < 4 || (migrationPhase == 5 && !isCompleted) {
            migrationDetailsStruct.Progress = "Schema migration"
        } else if migrationPhase < 6 || (migrationPhase <= 6 && !isCompleted) {
            migrationDetailsStruct.Progress = "Data migration"
        } else {
            migrationDetailsStruct.Progress = "Completed"
        }

        migrationDetailsStruct.InvocationTimestamp = invocation_ts.Format("2006-01-02 15:04:05")
        migrationDetailsStruct.MigrationName = "Migration_" +
            strings.Split(migrationDetailsStruct.MigrationUuid, "-")[4]
        
        if dbIp.Status == pgtype.Present {
            // dbIp determines whether port and db type are for source or target
            var dbIpStruct DbIp
            err = json.Unmarshal([]byte(dbIp.String), &dbIpStruct)
            if dbIpStruct.SourceDbIp != "" {
                migrationDetailsStruct.SourceDb.Ip = dbIpStruct.SourceDbIp
                if dbPort.Status == pgtype.Present && dbPort.Int != 0 {
                    migrationDetailsStruct.SourceDb.Port =
                        strconv.FormatInt(int64(dbPort.Int), 10)
                }
                if dbType.Status == pgtype.Present && dbType.String != "" {
                    migrationDetailsStruct.SourceDb.Engine = dbType.String
                }
                if dbVersion.Status == pgtype.Present && dbVersion.String != "" {
                    migrationDetailsStruct.SourceDb.Version = dbVersion.String
                }
            }
            if dbIpStruct.TargetDbIp != "" {
                migrationDetailsStruct.TargetCluster.Ip = dbIpStruct.TargetDbIp
                if dbPort.Status == pgtype.Present && dbPort.Int != 0 {
                    migrationDetailsStruct.TargetCluster.Port =
                        strconv.FormatInt(int64(dbPort.Int), 10)
                }
                if dbType.Status == pgtype.Present && dbType.String != "" {
                    migrationDetailsStruct.TargetCluster.Engine = dbType.String
                }
                if dbVersion.Status == pgtype.Present && dbVersion.String != "" {
                    migrationDetailsStruct.TargetCluster.Version = dbVersion.String
                }
            }
        }

        if voyagerInfo.Status == pgtype.Present {
            var voyagerInfoStruct VoyagerInfo
            err = json.Unmarshal([]byte(voyagerInfo.String), &voyagerInfoStruct)
            migrationDetailsStruct.Voyager.MachineIp = voyagerInfoStruct.Ip
            migrationDetailsStruct.Voyager.Os = voyagerInfoStruct.Os
            migrationDetailsStruct.Voyager.AvailDiskBytes =
                strconv.FormatUint(voyagerInfoStruct.AvailDisk, 10)

            // This is hard-coded in yb-voyager as the schema export directory
            if migrationDetailsStruct.MigrationPhase >= 2 {
                migrationDetailsStruct.Voyager.ExportedSchemaLocation =
                    filepath.Join(migrationDetailsStruct.Voyager.ExportDir, "schema")
            }
        }

        // For now, only support offline migrations
        migrationDetailsStruct.MigrationType = "Offline"

        // This value will be overwritten by the complexity in the migration assessment
        migrationDetailsStruct.Complexity = "N/A"
        if complexity.Status == pgtype.Present {
            migrationDetailsStruct.Complexity = complexity.String
        }
    }
    return nil
}

func updateMigrationDetailStructStartTimestamp(log logger.Logger, conn *pgxpool.Pool,
    migrationDetailsStruct *models.VoyagerMigrationDetails, migrationUuid string,
    migrationPhase int32, invocationSeq int32) error {

    voyagerDetailsrows, err := conn.Query(context.Background(),
        RETRIEVE_VOYAGER_MIGRATION_START_TIMESTAMP, migrationUuid, migrationPhase, invocationSeq)
    if err != nil {
        log.Errorf(fmt.Sprintf("[%s] Error while querying for migration details",
            LOGGER_FILE_NAME))
        log.Errorf(err.Error())
        return err
    }

    for voyagerDetailsrows.Next() {
        var start_ts time.Time

        err = voyagerDetailsrows.Scan(&start_ts)
        if err != nil {
            log.Errorf(fmt.Sprintf("[%s] Error while scanning migration details",
                LOGGER_FILE_NAME))
            log.Errorf(err.Error())
            continue
        }
        migrationDetailsStruct.StartTimestamp = start_ts.Format("2006-01-02 15:04:05")
    }
    return nil
}

func getVoyagerDataMigrationMetricsFuture(log logger.Logger, conn *pgxpool.Pool,
    migrationUuid string, future chan VoyagerDataMigrationMetricsFuture) {

    voyagerDataMigrationMetricsResponse := VoyagerDataMigrationMetricsFuture{
        Metrics: []models.VoyagerMigrateDataMetricsDetails{},
        Error:   nil,
    }

    rows, err := conn.Query(context.Background(), RETRIEVE_DATA_MIGRATION_METRICS, migrationUuid)
    if err != nil {
        log.Errorf(fmt.Sprintf("[%s] Error while running query: "+
            "RETRIEVE_DATA_MIGRATION_METRICS", LOGGER_FILE_NAME))
        log.Errorf(err.Error())
        voyagerDataMigrationMetricsResponse.Error = err
        future <- voyagerDataMigrationMetricsResponse
        return
    }
    defer rows.Close()

    for rows.Next() {
        rowStruct := models.VoyagerMigrateDataMetricsDetails{}
        var invocation_ts time.Time
        err := rows.Scan(&rowStruct.MigrationUuid, &rowStruct.TableName, &rowStruct.SchemaName,
            &rowStruct.MigrationPhase, &rowStruct.Status, &rowStruct.CountLiveRows,
            &rowStruct.CountTotalRows, &invocation_ts)
        if err != nil {
            voyagerDataMigrationMetricsResponse.Error = err
            future <- voyagerDataMigrationMetricsResponse
            return
        }
        rowStruct.InvocationTimestamp = invocation_ts.Format("2006-01-02 15:04:05")
        tableName := strings.Split(rowStruct.TableName, ".")
        if len(tableName) > 1 {
            rowStruct.TableName = tableName[1]
        } else {
            rowStruct.TableName = tableName[0]
        }
        voyagerDataMigrationMetricsResponse.Metrics = append(
            voyagerDataMigrationMetricsResponse.Metrics, rowStruct)
    }

    err = rows.Err()
    if err != nil {
        voyagerDataMigrationMetricsResponse.Error = err
        future <- voyagerDataMigrationMetricsResponse
        return
    }
    future <- voyagerDataMigrationMetricsResponse
}

func getMigrateSchemaTaskInfoFuture(log logger.Logger, conn *pgxpool.Pool, migrationUuid string,
    future chan MigrateSchemaInfoFuture) {

    MigrateSchemaTaskInfoResponse := MigrateSchemaInfoFuture{
        Data:  models.MigrateSchemaTaskInfo{},
        Error: nil,
    }
    var migrateSchemaTaskInfo models.MigrateSchemaTaskInfo

    // find schema phases info for the given migrationUUID
    log.Infof(fmt.Sprintf("[%s] Executing Query: [%s]", LOGGER_FILE_NAME,
        RETRIEVE_MIGRATE_SCHEMA_PHASES_INFO))
    rows, err := conn.Query(context.Background(), RETRIEVE_MIGRATE_SCHEMA_PHASES_INFO,
        migrationUuid)
    if err != nil {
        log.Errorf(fmt.Sprintf("[%s] Error executing Query: [%s]", LOGGER_FILE_NAME,
            RETRIEVE_MIGRATE_SCHEMA_PHASES_INFO))
        log.Errorf(err.Error())
        MigrateSchemaTaskInfoResponse.Error = err
        future <- MigrateSchemaTaskInfoResponse
    }

    var schemaPhaseInfoList []MigrateSchemaPhasesInfo
    for rows.Next() {
        rowStruct := MigrateSchemaPhasesInfo{}
        err := rows.Scan(&rowStruct.migrationUuid, &rowStruct.migrationPhase,
            &rowStruct.invocationSeq)
        if err != nil {
            log.Errorf(fmt.Sprintf("[%s] Error while scanning rows for "+
                "RETRIEVE_MIGRATE_SCHEMA_PHASES_INFO", LOGGER_FILE_NAME))
            log.Errorf(err.Error())
        }
        schemaPhaseInfoList = append(schemaPhaseInfoList, rowStruct)
    }

    migrateSchemaTaskInfo.MigrationUuid = migrationUuid
    determineStatusOfMigrateSchmeaPhases(log, conn, &schemaPhaseInfoList,
        &migrateSchemaTaskInfo)

    recommendedRefactoringList := []models.RefactoringCount{}
    for _, sqlObject := range migrateSchemaTaskInfo.SqlObjects {
        var refactorCount models.RefactoringCount
        refactorCount.SqlObjectType = sqlObject.ObjectType
        refactorCount.Automatic = sqlObject.TotalCount - sqlObject.InvalidCount
        refactorCount.Manual = sqlObject.InvalidCount
        recommendedRefactoringList = append(recommendedRefactoringList, refactorCount)
    }
    migrateSchemaTaskInfo.CurrentAnalysisReport.RecommendedRefactoring.RefactorDetails =
        recommendedRefactoringList

    conversionIssuesForSuggestedRefacotring := map[string]models.UnsupportedSqlWithDetails{}
    for _, conversionIssue := range migrateSchemaTaskInfo.SuggestionsErrors {

        if conversionIssue.IssueType == "migration_caveats" {
            conversionIssuesByIssueType, ok :=
                conversionIssuesForSuggestedRefacotring[conversionIssue.IssueType]

            if ok {
                conversionIssuesByIssueType.Count = conversionIssuesByIssueType.Count + 1
                conversionIssuesByIssueType.SuggestionsErrors = append(
                    conversionIssuesByIssueType.SuggestionsErrors, conversionIssue)
                conversionIssuesForSuggestedRefacotring[conversionIssue.IssueType] =
                    conversionIssuesByIssueType
            } else {
                var newConversionIssuesByType models.UnsupportedSqlWithDetails
                newConversionIssuesByType.Count = 1
                newConversionIssuesByType.UnsupportedType = MIGRATION_CAVEATS_UI_DISPLAY_STR
                newConversionIssuesByType.SuggestionsErrors =
                    append(newConversionIssuesByType.SuggestionsErrors, conversionIssue)
                conversionIssuesForSuggestedRefacotring[conversionIssue.IssueType] =
                    newConversionIssuesByType
            }

        } else {

            conversionIssuesByObjectType, ok :=
            conversionIssuesForSuggestedRefacotring[conversionIssue.ObjectType]
            if ok {
                conversionIssuesByObjectType.Count = conversionIssuesByObjectType.Count + 1
                conversionIssuesByObjectType.SuggestionsErrors = append(
                    conversionIssuesByObjectType.SuggestionsErrors, conversionIssue)
                conversionIssuesForSuggestedRefacotring[conversionIssue.ObjectType] =
                    conversionIssuesByObjectType
            } else {
                var newConversionIssuesByType models.UnsupportedSqlWithDetails
                newConversionIssuesByType.Count = 1
                newConversionIssuesByType.UnsupportedType = conversionIssue.ObjectType
                newConversionIssuesByType.SuggestionsErrors =
                    append(newConversionIssuesByType.SuggestionsErrors, conversionIssue)
                conversionIssuesForSuggestedRefacotring[conversionIssue.ObjectType] =
                    newConversionIssuesByType
            }

        }
    }

    var conversionIssuesDetailsWithCountList []models.UnsupportedSqlWithDetails
    for _, value := range conversionIssuesForSuggestedRefacotring {
        conversionIssuesDetailsWithCountList = append(conversionIssuesDetailsWithCountList, value)
    }

    migrateSchemaTaskInfo.CurrentAnalysisReport.UnsupportedFeatures =
        conversionIssuesDetailsWithCountList

    MigrateSchemaTaskInfoResponse.Data = migrateSchemaTaskInfo
    future <- MigrateSchemaTaskInfoResponse
}

func determineStatusOfMigrateSchmeaPhases(log logger.Logger, conn *pgxpool.Pool,
    schemaPhaseInfoList *[]MigrateSchemaPhasesInfo,
    migrateSchemaTaskInfo *models.MigrateSchemaTaskInfo) {

    // determine status of each migrate phase for a given migrationUUID
    migrateSchemaTaskInfo.ExportSchema = "N/A"
    migrateSchemaTaskInfo.AnalyzeSchema = "N/A"
    migrateSchemaTaskInfo.ImportSchema = "N/A"
    migrateSchemaTaskInfo.OverallStatus = "in-progress"
    for _, phaseInfo := range *schemaPhaseInfoList {
        switch phaseInfo.migrationPhase {
        case 2:
            if phaseInfo.invocationSeq%2 == 0 {
                migrateSchemaTaskInfo.ExportSchema = "complete"
            } else {
                migrateSchemaTaskInfo.ExportSchema = "in-progress"
            }
        case 3:
            if phaseInfo.invocationSeq >= 2 {
                var invocationSqToBeUsed int
                if phaseInfo.invocationSeq%2 == 0 {
                    invocationSqToBeUsed = phaseInfo.invocationSeq
                } else {
                    invocationSqToBeUsed = phaseInfo.invocationSeq - 1
                }
                future := make(chan MigrateSchemaUIDetailFuture)
                go fetchMigrateSchemaUIDetailsByUUID(log, conn, phaseInfo.migrationUuid,
                    phaseInfo.migrationPhase, invocationSqToBeUsed, future)
                migrateSchemaUIDetails := <-future
                if migrateSchemaUIDetails.Error != nil {
                    log.Errorf(fmt.Sprintf("[%s] Error while fetching migrate schema UI Details",
                        LOGGER_FILE_NAME))
                    log.Errorf(migrateSchemaUIDetails.Error.Error())
                    migrateSchemaTaskInfo.SuggestionsErrors = nil
                    migrateSchemaTaskInfo.SqlObjects = nil
                    migrateSchemaTaskInfo.AnalyzeSchema = "N/A"
                    continue
                }
                migrateSchemaTaskInfo.SuggestionsErrors = migrateSchemaUIDetails.SuggestionsErrors
                migrateSchemaTaskInfo.SqlObjects = migrateSchemaUIDetails.SqlObjects

                migrateSchemaTaskInfo.AnalyzeSchema = "complete"
            } else {
                migrateSchemaTaskInfo.ExportSchema = "in-progress"
            }
        case 5:
            future := make(chan string)
            go fetchSchemaImportStatus(log, conn, phaseInfo.migrationUuid,
                phaseInfo.migrationPhase, phaseInfo.invocationSeq, future)
            importSchemaStatus := <- future

            if importSchemaStatus == "COMPLETED" {
                migrateSchemaTaskInfo.ImportSchema = "complete"
                migrateSchemaTaskInfo.OverallStatus = "complete"
            } else {
                migrateSchemaTaskInfo.ImportSchema = "in-progress"
            }
        default:
            migrateSchemaTaskInfo.ExportSchema = "N/A"
            migrateSchemaTaskInfo.AnalyzeSchema = "N/A"
            migrateSchemaTaskInfo.ImportSchema = "N/A"
        }
    }
}

func fetchSchemaImportStatus(log logger.Logger, conn *pgxpool.Pool, migrationUuid string,
    migrationPhase int, invocationSq int, future chan string) {
        log.Infof(fmt.Sprintf("[%s] Executing Query: [%s]", LOGGER_FILE_NAME,
            RETRIEVE_IMPORT_SCHEMA_STATUS))
        var schemaImportStatus string
        row := conn.QueryRow(context.Background(), RETRIEVE_IMPORT_SCHEMA_STATUS, migrationUuid,
                                migrationPhase, invocationSq)
        err := row.Scan(&schemaImportStatus)
        log.Infof(fmt.Sprintf("schema import status: [%s]", schemaImportStatus))
        if err != nil {
            log.Errorf(fmt.Sprintf("[%s] Error while scaning results for query: [%s]",
                LOGGER_FILE_NAME, "RETRIEVE_ALL_VOYAGER_MIGRATIONS_SQL"))
            log.Errorf(err.Error())
            future <- "none"
            return
        }
        future <- schemaImportStatus
    }

func fetchMigrateSchemaUIDetailsByUUID(log logger.Logger, conn *pgxpool.Pool, migrationUuid string,
    migrationPhase int, invocationSq int, future chan MigrateSchemaUIDetailFuture) {

    MigrateSchemaUIDetailResponse := MigrateSchemaUIDetailFuture{
        SuggestionsErrors: []models.ErrorsAndSuggestionsDetails{},
        SqlObjects:        []models.SqlObjectsDetails{},
        Error:             nil,
    }

    log.Infof(fmt.Sprintf("[%s] Executing Query: [%s]", LOGGER_FILE_NAME,
        RETRIEVE_ANALYZE_SCHEMA_PAYLOAD))
    rows, err := conn.Query(context.Background(), RETRIEVE_ANALYZE_SCHEMA_PAYLOAD,
        migrationUuid, migrationPhase, invocationSq)
    if err != nil {
        log.Errorf(fmt.Sprintf("[%s] Error executing Query: [%s]", LOGGER_FILE_NAME,
            RETRIEVE_ANALYZE_SCHEMA_PAYLOAD))
        log.Errorf(err.Error())
        MigrateSchemaUIDetailResponse.Error = err
        future <- MigrateSchemaUIDetailResponse
        return
    }
    defer rows.Close()

    var schemaAnalyzeReport PayloadReport
    var payload string
    for rows.Next() {
        err := rows.Scan(&payload)
        if err != nil {
            log.Errorf(fmt.Sprintf("[%s] Error during row scan", LOGGER_FILE_NAME))
            log.Errorf(err.Error())
            MigrateSchemaUIDetailResponse.SqlObjects = nil
            MigrateSchemaUIDetailResponse.SuggestionsErrors = nil
            future <- MigrateSchemaUIDetailResponse
            return
        }
    }

    err = json.Unmarshal([]byte(payload), &schemaAnalyzeReport)
    if err != nil {
        log.Errorf(fmt.Sprintf("[%s] Error parsing schema analysis report",
            LOGGER_FILE_NAME))
        log.Errorf(err.Error())
        MigrateSchemaUIDetailResponse.SqlObjects = nil
        MigrateSchemaUIDetailResponse.SuggestionsErrors = nil
        future <- MigrateSchemaUIDetailResponse
        return
    }
    MigrateSchemaUIDetailResponse.SqlObjects = schemaAnalyzeReport.Summary.DatabaseObjects
    MigrateSchemaUIDetailResponse.SuggestionsErrors = schemaAnalyzeReport.Issues

    future <- MigrateSchemaUIDetailResponse
}

func extractValue(line string) string {
    parts := strings.SplitN(line, "=", 2)
    if len(parts) != 2 {
        return ""
    }
    return strings.Trim(strings.TrimSpace(parts[1]), "\"")
}

func (c *Container) GetVoyagerAssesmentDetails(ctx echo.Context) error {

    migrationAssesmentInfo := models.MigrationAssesmentInfo{}
    assesmentComplexityInfo := []models.AssesmentComplexityInfo{}
    migrationAssesmentInfo.ComplexityOverview = assesmentComplexityInfo
    migrationAssesmentInfo.TopErrors = []string{}
    migrationAssesmentInfo.TopSuggestions = []string{}
    migrationAssesmentInfo.AssesmentStatus = false

    conn, err := c.GetConnection("yugabyte")
    if err != nil {
        return ctx.String(http.StatusInternalServerError, err.Error())
    }
    // Detect operating system
    var osName string
    if strings.EqualFold(runtime.GOOS, "linux") {
      // Check if running in Docker
      dockerFile, errDocker := os.ReadFile("/proc/1/cgroup")
      if errDocker == nil && strings.EqualFold(string(dockerFile), "docker") {
          osName = "docker"
      } else {
          file, errLinux := os.ReadFile("/etc/os-release")
          if errLinux != nil {
              c.logger.Errorf("Failed to read /etc/os-release: %v", errLinux)
              osName = "unknown"
          } else {
              var versionID string
              lines := strings.Split(string(file), "\n")
              for _, line := range lines {
                  if strings.HasPrefix(line, "ID=") {
                      osName = extractValue(line)
                  }
                  if strings.HasPrefix(line, "VERSION_ID=") {
                      versionID = extractValue(line)
                  }
                  if osName != "" && versionID != "" {
                      break
                  }
              }

              if strings.EqualFold(osName, "ubuntu") {
                  if strings.HasPrefix(versionID, "22.") {
                      osName = "ubuntu"
                  } else {
                      osName = "UbuntuGeneric"
                  }
              } else {
                  distrosList := []string{"centos", "almalinux", "rhel"}
                  isRHELDistro := false
                  for _, distro := range distrosList {
                      if strings.EqualFold(distro, osName) {
                          isRHELDistro = true
                          break
                      }
                  }
                  if !isRHELDistro {
                      osName = "INVALID_OS"
                  }
              }
          }
      }
    } else if strings.EqualFold(runtime.GOOS, "darwin") {
      osName = "darwin"
    }
    migrationAssesmentInfo.OperatingSystem = osName

    // Compute the top errors and suggestions
    var migrationUuid string
    migrationUuid = ctx.QueryParam("uuid")
    future := make(chan MigrateSchemaUIDetailFuture)
    go fetchMigrateSchemaUIDetailsByUUID(c.logger, conn, migrationUuid, 1, 2, future)
    migrateSchemaUIDetails := <-future
    if migrateSchemaUIDetails.Error != nil {
        c.logger.Errorf(fmt.Sprintf("[%s] Error while fetching migrate schema UI Details",
            LOGGER_FILE_NAME))
        c.logger.Errorf(migrateSchemaUIDetails.Error.Error())
    }

    if migrateSchemaUIDetails.SqlObjects == nil {
        return ctx.JSON(http.StatusOK, migrationAssesmentInfo)
    }

    //TO-DO: rank the errors and suggestions
    var errors = make(map[string]int)
    var suggestions = make(map[string]int)
    for _, suggestionsError := range migrateSchemaUIDetails.SuggestionsErrors {
        errors[suggestionsError.Reason]++
        suggestions[suggestionsError.Suggestion]++
    }

    for key := range errors {
        migrationAssesmentInfo.TopErrors = append(migrationAssesmentInfo.TopErrors, key)
    }

    for key := range suggestions {
        migrationAssesmentInfo.TopSuggestions = append(migrationAssesmentInfo.TopSuggestions, key)
    }

    var assesmentComplexity models.AssesmentComplexityInfo
    var sqlObjectCount int32
    for _, sqlObject := range migrateSchemaUIDetails.SqlObjects {
        if sqlObject.ObjectType == "TABLE" {
            assesmentComplexity.TableCount = sqlObject.TotalCount
        }
        sqlObjectCount = sqlObjectCount + sqlObject.TotalCount
    }
    assesmentComplexity.SqlObjectsCount = sqlObjectCount

    // Fetching schmea and complexity details
    voyagerDetailsrows, err := conn.Query(context.Background(),
        RETRIEVE_VOYAGER_MIGRATION_DETAILS, migrationUuid, 0, 2)
    if err != nil {
        c.logger.Errorf(fmt.Sprintf("[%s] Error while querying for assesment details",
            LOGGER_FILE_NAME))
        c.logger.Errorf(err.Error())
    }

    for voyagerDetailsrows.Next() {
        rowStruct := models.VoyagerMigrationDetails{}
        var invocation_ts time.Time
        var complexity pgtype.Text
        err = voyagerDetailsrows.Scan(&rowStruct.SourceDb.Database, &rowStruct.SourceDb.Schema,
            &rowStruct.Status, &invocation_ts, &complexity, &rowStruct.SourceDb.Engine)
        if err != nil {
            c.logger.Errorf(fmt.Sprintf("[%s] Error while scanning for assesment details",
                LOGGER_FILE_NAME))
            c.logger.Errorf(err.Error())
        }
        assesmentComplexity.Schema = rowStruct.SourceDb.Schema
        assesmentComplexity.Complexity = complexity.String
    }

    assesmentComplexityInfo = append(assesmentComplexityInfo, assesmentComplexity)
    migrationAssesmentInfo.ComplexityOverview = assesmentComplexityInfo
    migrationAssesmentInfo.AssesmentStatus = true

    return ctx.JSON(http.StatusOK, migrationAssesmentInfo)
}

func (c *Container) GetVoyagerAssessmentReport(ctx echo.Context) error {

    migrationUuid := ctx.QueryParam("uuid")

    conn, err := c.GetConnection("yugabyte")
    if err != nil {
        return ctx.String(http.StatusInternalServerError, err.Error())
    }

    future := make(chan AssessmentReportQueryFuture)
    go getMigrationAssessmentReportFuture(c.logger, migrationUuid, conn, future)

    voyagerAssessmentReportResponse := models.MigrationAssessmentReport{
        AssessmentStatus:       true,
        Summary:                models.AssessmentReportSummary{},
        SourceEnvironment:      models.SourceEnvironmentInfo{},
        SourceDatabase:         models.SourceDatabaseInfo{},
        TargetRecommendations:  models.TargetClusterRecommendationDetails{},
        RecommendedRefactoring: models.RecommendedRefactoringGraph{},
        UnsupportedDataTypes:   []models.UnsupportedSqlInfo{},
        UnsupportedFunctions:   []models.UnsupportedSqlInfo{},
        UnsupportedFeatures:    []models.UnsupportedSqlInfo{},
    }

    assessmentReportVisualisationData := <- future
    if assessmentReportVisualisationData.Error != nil {
        if assessmentReportVisualisationData.Error.Error() == pgx.ErrNoRows.Error() {
            voyagerAssessmentReportResponse.AssessmentStatus = false
            return ctx.JSON(http.StatusOK, voyagerAssessmentReportResponse)
        } else {
            return ctx.String(http.StatusInternalServerError,
                assessmentReportVisualisationData.Error.Error())
        }
    }

    assessmentReport := assessmentReportVisualisationData.Report.AssessmentJsonReport

    estimatedTime := int64(math.Round(
        assessmentReport.Sizing.SizingRecommendation.EstimatedTimeInMinForImport))
    voyagerAssessmentReportResponse.Summary.EstimatedMigrationTime =
        fmt.Sprintf("%v Minutes", estimatedTime)
    voyagerAssessmentReportResponse.Summary.MigrationComplexity =
        assessmentReportVisualisationData.Report.MigrationComplexity
    voyagerAssessmentReportResponse.Summary.Summary =
        assessmentReport.Sizing.SizingRecommendation.ColocatedReasoning

    voyagerAssessmentReportResponse.SourceEnvironment.TotalVcpu = ""
    voyagerAssessmentReportResponse.SourceEnvironment.TotalMemory = ""
    voyagerAssessmentReportResponse.SourceEnvironment.TotalDiskSize = ""
    voyagerAssessmentReportResponse.SourceEnvironment.NoOfConnections = ""

    voyagerAssessmentReportResponse.SourceDatabase.TableSize =
        assessmentReportVisualisationData.Report.SourceSizeDetails.TotalTableSize
    voyagerAssessmentReportResponse.SourceDatabase.TableRowCount =
        assessmentReportVisualisationData.Report.SourceSizeDetails.TotalTableRowCount
    voyagerAssessmentReportResponse.SourceDatabase.TotalTableSize =
        assessmentReportVisualisationData.Report.SourceSizeDetails.TotalDBSize
    voyagerAssessmentReportResponse.SourceDatabase.TotalIndexSize =
        assessmentReportVisualisationData.Report.SourceSizeDetails.TotalIndexSize


    voyagerAssessmentReportResponse.TargetRecommendations.RecommendationSummary =
        assessmentReport.Sizing.SizingRecommendation.ColocatedReasoning
    voyagerAssessmentReportResponse.TargetRecommendations.
        TargetClusterRecommendation.NumNodes =
        int64(assessmentReport.Sizing.SizingRecommendation.NumNodes)
    voyagerAssessmentReportResponse.TargetRecommendations.
        TargetClusterRecommendation.VcpuPerNode =
        int64(assessmentReport.Sizing.SizingRecommendation.VCPUsPerInstance)
    voyagerAssessmentReportResponse.TargetRecommendations.
        TargetClusterRecommendation.MemoryPerNode =
        int64(assessmentReport.Sizing.SizingRecommendation.MemoryPerInstance)
    voyagerAssessmentReportResponse.TargetRecommendations.
        TargetClusterRecommendation.InsertsPerNode =
        assessmentReport.Sizing.SizingRecommendation.OptimalInsertConnectionsPerNode
    voyagerAssessmentReportResponse.TargetRecommendations.
        TargetClusterRecommendation.ConnectionsPerNode =
        assessmentReport.Sizing.SizingRecommendation.OptimalSelectConnectionsPerNode

    voyagerAssessmentReportResponse.TargetRecommendations.
        TargetSchemaRecommendation.NoOfColocatedTables =
            int64(len(assessmentReport.Sizing.SizingRecommendation.ColocatedTables))
    voyagerAssessmentReportResponse.TargetRecommendations.
        TargetSchemaRecommendation.NoOfShardedTables =
            int64(len(assessmentReport.Sizing.SizingRecommendation.ShardedTables))
    voyagerAssessmentReportResponse.TargetRecommendations.
        TargetSchemaRecommendation.TotalSizeColocatedTables =
            assessmentReportVisualisationData.Report.
                TargetSizingRecommendations.TotalColocatedSize
    voyagerAssessmentReportResponse.TargetRecommendations.
        TargetSchemaRecommendation.TotalSizeShardedTables =
            assessmentReportVisualisationData.Report.
                TargetSizingRecommendations.TotalShardedSize

    dbObjectsMap := map[string]int{}
    for _, dbObject := range assessmentReport.SchemaSummary.DBObjects {
       dbObjectsMap[dbObject.ObjectType] = dbObject.TotalCount
    }

    dbObjectConversionIssuesMap := map[string]int{}
    for _, conversionIssue := range assessmentReportVisualisationData.Report.ConversionIssues {
        count, ok := dbObjectConversionIssuesMap[conversionIssue.ObjectType]
        if ok {
            count++
            dbObjectConversionIssuesMap[conversionIssue.ObjectType] = count
        } else {
            dbObjectConversionIssuesMap[conversionIssue.ObjectType] = 1
        }
    }

    recommendedRefactoringList := []models.RefactoringCount{}
    for sqlObjectType, sqlObjectcount := range dbObjectsMap {
        var refactorCount models.RefactoringCount
        refactorCount.SqlObjectType = sqlObjectType
        issueCount, ok := dbObjectConversionIssuesMap[sqlObjectType]
        if ok {
            refactorCount.Automatic = int32(sqlObjectcount - issueCount)
            refactorCount.Manual = int32(issueCount)
        } else {
            refactorCount.Automatic = int32(sqlObjectcount)
            refactorCount.Manual = 0
        }
        recommendedRefactoringList = append(recommendedRefactoringList, refactorCount)
    }

    voyagerAssessmentReportResponse.RecommendedRefactoring.RefactorDetails =
            recommendedRefactoringList

    unsupportedDataTypeMap := make(map[string]models.UnsupportedSqlInfo)
    for _, unsupportedDataType := range assessmentReport.UnsupportedDataTypes {

        column := fmt.Sprintf("%s.%s.%s", unsupportedDataType.SchemaName,
            unsupportedDataType.TableName, unsupportedDataType.ColumnName)
        _, ok := unsupportedDataTypeMap[unsupportedDataType.DataType]
        if ok {
            unsupportedDataTypeInfo := unsupportedDataTypeMap[unsupportedDataType.DataType]
            unsupportedDataTypeInfo.Count += 1
            unsupportedDataTypeInfo.Objects = append(unsupportedDataTypeInfo.Objects,
                models.UnsupportedSqlObjectData{
                    ObjectName: column,
                    SqlStatement: column,
                },
            )
            unsupportedDataTypeMap[unsupportedDataType.DataType] = unsupportedDataTypeInfo
        } else {
            unsupportedDataTypeInfo := models.UnsupportedSqlInfo{
                UnsupportedType: unsupportedDataType.DataType,
                Count: 1,
                Objects: []models.UnsupportedSqlObjectData{
                    models.UnsupportedSqlObjectData{
                        ObjectName: column,
                        SqlStatement: column,
                    },
                },
                DocsLink: "",
            }
            unsupportedDataTypeMap[unsupportedDataType.DataType] = unsupportedDataTypeInfo
        }
    }

    var unsupportedDataTypesList []models.UnsupportedSqlInfo
    for _, unsupportedDataType := range unsupportedDataTypeMap {
        unsupportedDataTypesList = append(unsupportedDataTypesList, unsupportedDataType)
    }

    voyagerAssessmentReportResponse.UnsupportedDataTypes = unsupportedDataTypesList

    // Convert the backend model []UnsupportedFeatures into UX model []UnsupportedSqlInfo
    // to-do: Need to confirm with Voyager team on mapping
    var unsupportedFeaturesList []models.UnsupportedSqlInfo
    for _, unsupportedFeatureType := range assessmentReport.UnsupportedFeatures{
        unsupportedFeature := models.UnsupportedSqlInfo{}
        unsupportedFeature.UnsupportedType = unsupportedFeatureType.FeatureName
        unsupportedFeature.Count = int32(len(unsupportedFeatureType.Objects))
        unsupportedFeature.DocsLink = unsupportedFeatureType.DocsLink
        unsupportedFeature.Objects = []models.UnsupportedSqlObjectData{}
        for _, obj := range unsupportedFeatureType.Objects {
            unsupportedFeature.Objects = append(unsupportedFeature.Objects,
                models.UnsupportedSqlObjectData{
                    ObjectName: obj.ObjectName,
                    SqlStatement: obj.SqlStatement,
                },
            )
        }
        if (unsupportedFeature.Count != 0) {
            unsupportedFeaturesList = append(unsupportedFeaturesList, unsupportedFeature)
        }
    }
    voyagerAssessmentReportResponse.UnsupportedFeatures = unsupportedFeaturesList

    return ctx.JSON(http.StatusOK, voyagerAssessmentReportResponse)
}

func getMigrationAssessmentReportFuture(log logger.Logger, migrationUuid string, conn *pgxpool.Pool,
    future chan AssessmentReportQueryFuture) {

        MigrationAsessmentReportResponse := AssessmentReportQueryFuture{
            Report: helpers.AssessmentVisualisationMetadata{},
            Error: nil,
        }
        log.Infof(fmt.Sprintf("migration uuid: %s", migrationUuid))
        var assessmentReportPayload string
        var assessmentVisualisationData helpers.AssessmentVisualisationMetadata
        row := conn.QueryRow(context.Background(), RETRIEVE_ASSESSMENT_REPORT, migrationUuid)

        err := row.Scan(&assessmentReportPayload)

        if err != nil {

            log.Errorf(fmt.Sprintf("[%s] Error while scaning results for query: [%s]",
                LOGGER_FILE_NAME, "RETRIEVE_ASSESSMENT_REPORT"))
            log.Errorf(err.Error())
            MigrationAsessmentReportResponse.Error = err
            future <- MigrationAsessmentReportResponse
            return
        }

        err = json.Unmarshal([]byte(assessmentReportPayload), &assessmentVisualisationData)
        if err != nil {
            log.Errorf(fmt.Sprintf("[%s] Error while JSON Unmarshal of the assessment report.",
                    LOGGER_FILE_NAME))
            log.Errorf(err.Error())
        }

        MigrationAsessmentReportResponse.Report = assessmentVisualisationData
        future <- MigrationAsessmentReportResponse
    }

    func (c *Container) GetAssessmentSourceDBDetails(ctx echo.Context) error {

        migrationUuid := ctx.QueryParam("uuid")

        conn, err := c.GetConnection("yugabyte")
        if err != nil {
            return ctx.String(http.StatusInternalServerError, err.Error())
        }

        future := make(chan AssessmentReportQueryFuture)
        go getMigrationAssessmentReportFuture(c.logger, migrationUuid, conn, future)

        assessmentSourceDBDetails := models.AssessmentSourceDbObject {
            SqlObjectsCount: []models.SqlObjectCount{},
            SqlObjectsMetadata: []models.SqlObjectMetadata{},
        }

        assessmentReportVisualisationData := <- future

        if assessmentReportVisualisationData.Error != nil {
            if assessmentReportVisualisationData.Error.Error() == pgx.ErrNoRows.Error() {
                return ctx.JSON(http.StatusOK, assessmentSourceDBDetails)
            } else {
                return ctx.String(http.StatusInternalServerError,
                    assessmentReportVisualisationData.Error.Error())
            }
        }

        assessmentReport := assessmentReportVisualisationData.Report.AssessmentJsonReport

        sqlMetadataList := []models.SqlObjectMetadata{}
        tableIndexList := assessmentReport.TableIndexStats
        setForSchemaObjectPairs := map[string]int{}
        for _, dbObjectStat := range assessmentReport.SchemaSummary.DBObjects {
          var sqlMetadata1 models.SqlObjectMetadata
          allObjectNames := dbObjectStat.ObjectNames
          allObjectNamesArray := strings.Split(allObjectNames, ", ")
          for _, currentObjName := range allObjectNamesArray {
            sqlMetadata1.SqlType = strings.ToLower(dbObjectStat.ObjectType)

            sqlMetadata1.Size = -1
            sqlMetadata1.Iops = -1
            sqlMetadata1.ObjectName = currentObjName
            setForSchemaObjectPairs[strings.ToLower(currentObjName)] = len(sqlMetadataList)
            sqlMetadataList = append(sqlMetadataList, sqlMetadata1)
          }
        }

        for _, dbObjectStat := range tableIndexList {
          currentObjectName := string(dbObjectStat.SchemaName) + "." +
            string(dbObjectStat.ObjectName)
          // Here searching with and without schema names is important because of inconsistent
          // payload
          objectNameLower := strings.ToLower(currentObjectName)
          objectNameOnlyLower := strings.ToLower(string(dbObjectStat.ObjectName))
          index, exists := setForSchemaObjectPairs[objectNameLower]
          if !exists {
            index, exists = setForSchemaObjectPairs[objectNameOnlyLower]
          }
          if exists {
            sqlMetadataList[index].Iops = dbObjectStat.Reads
            sqlMetadataList[index].Size = dbObjectStat.SizeInBytes
            sqlMetadataList[index].ObjectName = currentObjectName
          }
        }

        assessmentSourceDBDetails.SqlObjectsMetadata = sqlMetadataList

        sqlObjectsList := []models.SqlObjectCount{}

        dbObjectsFromReport :=
            assessmentReportVisualisationData.Report.AssessmentJsonReport.SchemaSummary.DBObjects
        for _, value := range dbObjectsFromReport {
            var sqlObject1 models.SqlObjectCount
            sqlObject1.SqlType = value.ObjectType
            sqlObject1.Count = int32(value.TotalCount)
            sqlObjectsList = append(sqlObjectsList, sqlObject1)
        }

        assessmentSourceDBDetails.SqlObjectsCount = sqlObjectsList
        return ctx.JSON(http.StatusOK, assessmentSourceDBDetails)
    }

    func (c *Container) GetTargetRecommendations(ctx echo.Context) error {

        migrationUuid := ctx.QueryParam("uuid")

        conn, err := c.GetConnection("yugabyte")
        if err != nil {
            return ctx.String(http.StatusInternalServerError, err.Error())
        }

        future := make(chan AssessmentReportQueryFuture)
        go getMigrationAssessmentReportFuture(c.logger, migrationUuid, conn, future)

        assessmentReportVisualisationData := <- future

        targetRecommendationDetails := models.AssessmentTargetRecommendationObject {
            NumOfColocatedTables: 0,
            TotalSizeColocatedTables: 0,
            NumOfShardedTable: 0,
            TotalSizeShardedTables: 0,
            RecommendationDetails: []models.TargetRecommendationItem{},
        }

        if assessmentReportVisualisationData.Error != nil {
            if assessmentReportVisualisationData.Error.Error() == pgx.ErrNoRows.Error() {
                return ctx.JSON(http.StatusOK, targetRecommendationDetails)
            } else {
                return ctx.String(http.StatusInternalServerError,
                    assessmentReportVisualisationData.Error.Error())
            }
        }

        assessmentReport := assessmentReportVisualisationData.Report.AssessmentJsonReport

        targetRecommendationDetails.NumOfColocatedTables =
            int32(len(assessmentReport.Sizing.SizingRecommendation.ColocatedTables))
        targetRecommendationDetails.TotalSizeColocatedTables =
        assessmentReportVisualisationData.Report.
            TargetSizingRecommendations.TotalColocatedSize
        targetRecommendationDetails.NumOfShardedTable =
            int32(len(assessmentReport.Sizing.SizingRecommendation.ShardedTables))
        targetRecommendationDetails.TotalSizeShardedTables =
            assessmentReportVisualisationData.Report.
                TargetSizingRecommendations.TotalShardedSize

        tableRecommendations := map[string]string{}

        for _, value := range assessmentReport.Sizing.SizingRecommendation.ColocatedTables {
            tableName := strings.Split(value, ".")[1]
            tableRecommendations[tableName] = "Colocated"
        }

        for _, value := range assessmentReport.Sizing.SizingRecommendation.ShardedTables {
            tableName := strings.Split(value, ".")[1]
            tableRecommendations[tableName] = "Sharded"
        }

        recommendationsList := []models.TargetRecommendationItem{}
        for _, dbObjectStat := range assessmentReport.TableIndexStats {
            var recommendation1 models.TargetRecommendationItem
            _, ok := tableRecommendations[dbObjectStat.ObjectName]
            if ok {
                recommendation1.TableName = dbObjectStat.ObjectName
                recommendation1.SchemaRecommendation =
                    tableRecommendations[dbObjectStat.ObjectName]
                recommendation1.DiskSize = dbObjectStat.SizeInBytes
                recommendationsList = append(recommendationsList, recommendation1)
            }
        }
        targetRecommendationDetails.RecommendationDetails = recommendationsList

        return ctx.JSON(http.StatusOK, targetRecommendationDetails)
    }
