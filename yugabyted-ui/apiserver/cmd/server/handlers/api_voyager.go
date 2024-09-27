package handlers

import (
    "apiserver/cmd/server/helpers"
    "apiserver/cmd/server/logger"
    "apiserver/cmd/server/models"
    "context"
    "encoding/json"
    "fmt"
    "net/http"
    "path/filepath"
    "strconv"
    "strings"
    "time"
    "math"

    "github.com/jackc/pgtype"
    "github.com/jackc/pgx/v4/pgxpool"
    "github.com/labstack/echo/v4"
)

const LOGGER_FILE_NAME = "api_voyager"

// Gets one row for each unique migration_uuid, and also gets the highest migration_phase and
// invocation for import/export
const RETRIEVE_ALL_VOYAGER_MIGRATIONS_SQL string = 
`SELECT * FROM (
    SELECT migration_uuid, highest_export_phase, MAX(invocation_sequence) AS
    highest_export_invocation FROM (
        SELECT migration_uuid, MAX(migration_phase) AS
        highest_export_phase, invocation_sequence FROM (
            SELECT migration_uuid, migration_phase, invocation_sequence FROM
            ybvoyager_visualizer.ybvoyager_visualizer_metadata WHERE migration_phase <= 4
        ) AS export_phase_rows GROUP BY migration_uuid, invocation_sequence
    ) AS highest_export_rows GROUP BY migration_uuid, highest_export_phase
) AS export FULL JOIN 
(
    SELECT migration_uuid, highest_import_phase, MAX(invocation_sequence) AS
    highest_import_invocation FROM (
        SELECT migration_uuid, MAX(migration_phase) AS
        highest_import_phase, invocation_sequence FROM (
            SELECT migration_uuid, migration_phase, invocation_sequence FROM
            ybvoyager_visualizer.ybvoyager_visualizer_metadata WHERE migration_phase > 4
        ) AS export_phase_rows GROUP BY migration_uuid, invocation_sequence
    ) AS highest_import_rows GROUP BY migration_uuid, highest_import_phase
) AS import USING (migration_uuid) FULL JOIN
(
    SELECT migration_uuid, lowest_phase, MIN(invocation_sequence) AS
    lowest_invocation FROM (
        SELECT migration_uuid, MIN(migration_phase) AS lowest_phase, invocation_sequence FROM
        ybvoyager_visualizer.ybvoyager_visualizer_metadata GROUP BY migration_uuid, invocation_sequence
    ) AS lowest_rows GROUP BY migration_uuid, lowest_phase
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
    "WHERE migration_UUID=$1" +
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
    
    // TODO: remove this
    // future <- voyagerMigrationsResponse
    // return

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
        rowStruct := AllVoyagerMigrations{}
        err := rows.Scan(&rowStruct.migrationUuid, &rowStruct.exportMigrationPhase,
            &rowStruct.exportInvocationSeq, &rowStruct.importMigrationPhase,
            &rowStruct.importInvocationSeq, &rowStruct.lowestMigrationPhase,
            &rowStruct.lowestInvocationSeq,
        )
        if err != nil {
            log.Errorf(fmt.Sprintf("[%s] Error while scaning results for query: [%s]",
                LOGGER_FILE_NAME, "RETRIEVE_ALL_VOYAGER_MIGRATIONS_SQL"))
            log.Errorf(err.Error())
            continue
        }
        allVoyagerMigrationsList = append(allVoyagerMigrationsList, rowStruct)
    }

    for _, allVoyagerMigration := range allVoyagerMigrationsList {

        assessmentFuture := make(chan AssessmentReportQueryFuture)
        go getMigrationAssessmentReportFuture(log, allVoyagerMigration.migrationUuid, conn,
            assessmentFuture)

        // Update migration data struct using most recent import and export step.
        // This is done since source db info is only in export rows, target db info in import rows.
        // Info from import rows will overwrite info from export rows.
        rowStruct := models.VoyagerMigrationDetails{}
        if allVoyagerMigration.exportMigrationPhase.Status == pgtype.Present &&
            allVoyagerMigration.exportInvocationSeq.Status == pgtype.Present {
            err := updateRowStruct(log, conn, &rowStruct, allVoyagerMigration.migrationUuid,
                allVoyagerMigration.exportMigrationPhase.Int,
                allVoyagerMigration.exportInvocationSeq.Int)
            if err != nil {
                log.Errorf("[%s] Error while querying for export phase migration details",
                    LOGGER_FILE_NAME)
            }
        }
        if allVoyagerMigration.importMigrationPhase.Status == pgtype.Present &&
            allVoyagerMigration.importInvocationSeq.Status == pgtype.Present {
            err := updateRowStruct(log, conn, &rowStruct, allVoyagerMigration.migrationUuid,
                allVoyagerMigration.importMigrationPhase.Int,
                allVoyagerMigration.importInvocationSeq.Int)
            if err != nil {
                log.Errorf("[%s] Error while querying for import phase migration details",
                    LOGGER_FILE_NAME)
            }
        }
        if allVoyagerMigration.lowestMigrationPhase.Status == pgtype.Present &&
            allVoyagerMigration.lowestInvocationSeq.Status == pgtype.Present {
            err := updateRowStructStartTimestamp(log, conn, &rowStruct,
                allVoyagerMigration.migrationUuid,
                allVoyagerMigration.lowestMigrationPhase.Int,
                allVoyagerMigration.lowestInvocationSeq.Int)
            if err != nil {
                log.Errorf("[%s] Error while querying for migration start timestamp",
                    LOGGER_FILE_NAME)
            }
        }

        rowStruct.Complexity = "N/A"
        assessment := <-assessmentFuture
        if assessment.Error != nil {
            log.Errorf("[%s] Error getting migration assessment",
                LOGGER_FILE_NAME)
        } else {
            rowStruct.Complexity = assessment.Report.MigrationComplexity
        }
        voyagerMigrationsResponse.Migrations = append(
            voyagerMigrationsResponse.Migrations, rowStruct)
    }
    future <- voyagerMigrationsResponse
}

func updateRowStruct(log logger.Logger, conn *pgxpool.Pool,
    rowStruct *models.VoyagerMigrationDetails, migrationUuid string, migrationPhase int32,
    invocationSeq int32) error {

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

        rowStruct.MigrationUuid = migrationUuid
        rowStruct.MigrationPhase = migrationPhase
        rowStruct.InvocationSequence = invocationSeq
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

        if database.Status == pgtype.Present {
            rowStruct.SourceDb.Database = database.String
        }
        if schema.Status == pgtype.Present {
            rowStruct.SourceDb.Schema = schema.String
        }
        if status.Status == pgtype.Present {
            rowStruct.Status = status.String
        }
        if exportDir.Status == pgtype.Present {
            rowStruct.Voyager.ExportDir = exportDir.String
        }

        rowStruct.Status = "In Progress"
        if migrationPhase == 6 && invocationSeq >= 2 {
            rowStruct.Status = "Complete"
        }
        
        if migrationPhase <= 1 {
            rowStruct.Progress = "Assessment"
        } else if migrationPhase <= 5 {
            rowStruct.Progress = "Schema migration"
        } else {
            rowStruct.Progress = "Data migration"
        }

        rowStruct.InvocationTimestamp = invocation_ts.Format("2006-01-02 15:04:05")
        rowStruct.MigrationName = "Migration_" +
            strings.Split(rowStruct.MigrationUuid, "-")[4]
        
        if dbIp.Status == pgtype.Present {
            // dbIp determines whether port and db type are for source or target
            var dbIpStruct DbIp
            err = json.Unmarshal([]byte(dbIp.String), &dbIpStruct)
            if dbIpStruct.SourceDbIp != "" {
                rowStruct.SourceDb.Ip = dbIpStruct.SourceDbIp
                if dbPort.Status == pgtype.Present {
                    rowStruct.SourceDb.Port = strconv.FormatInt(int64(dbPort.Int), 10)
                }
                if dbType.Status == pgtype.Present {
                    rowStruct.SourceDb.Engine = dbType.String
                }
                if dbVersion.Status == pgtype.Present {
                    rowStruct.SourceDb.Version = dbVersion.String
                }
            }
            if dbIpStruct.TargetDbIp != "" {
                rowStruct.TargetCluster.Ip = dbIpStruct.TargetDbIp
                if dbPort.Status == pgtype.Present && rowStruct.TargetCluster.Ip != "" {
                    rowStruct.TargetCluster.Port = strconv.FormatInt(int64(dbPort.Int), 10)
                }
                if dbPort.Status == pgtype.Present && rowStruct.TargetCluster.Ip != "" {
                    rowStruct.TargetCluster.Engine = dbType.String
                }
                if dbVersion.Status == pgtype.Present {
                    rowStruct.TargetCluster.Version = dbVersion.String
                }
            }
        }

        if voyagerInfo.Status == pgtype.Present {
            var voyagerInfoStruct VoyagerInfo
            err = json.Unmarshal([]byte(voyagerInfo.String), &voyagerInfoStruct)
            rowStruct.Voyager.MachineIp = voyagerInfoStruct.Ip
            rowStruct.Voyager.Os = voyagerInfoStruct.Os
            rowStruct.Voyager.AvailDiskBytes = strconv.FormatUint(voyagerInfoStruct.AvailDisk, 10)

            // This is hard-coded in yb-voyager as the schema export directory
            if rowStruct.MigrationPhase >= 2 {
                rowStruct.Voyager.ExportedSchemaLocation =
                    filepath.Join(rowStruct.Voyager.ExportDir, "schema")
            }
        }

        // For now, only support offline migrations
        rowStruct.MigrationType = "Offline"

        rowStruct.Complexity = "N/A"
        if complexity.Status == pgtype.Present {
            rowStruct.Complexity = complexity.String
        } else {
            log.Infof(fmt.Sprintf("Complexity not set for migration uuid: [%s]",
                rowStruct.MigrationUuid))
            if rowStruct.MigrationPhase >= 1 && rowStruct.InvocationSequence >= 2 {
                go helpers.CalculateAndUpdateComplexity(log, conn,
                    rowStruct.MigrationUuid, 1, 2)
            }
        }
    }
    return nil
}

func updateRowStructStartTimestamp(log logger.Logger, conn *pgxpool.Pool,
    rowStruct *models.VoyagerMigrationDetails, migrationUuid string, migrationPhase int32,
    invocationSeq int32) error {

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
        rowStruct.StartTimestamp = start_ts.Format("2006-01-02 15:04:05")
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

    conversionIssuesDetailsWithCountMap := map[string]models.UnsupportedSqlWithDetails{}
    fmt.Println(migrateSchemaTaskInfo.SuggestionsErrors)
    for _, conversionIssue := range migrateSchemaTaskInfo.SuggestionsErrors {
        fmt.Println(conversionIssue)
        conversionIssuesByType, ok :=
            conversionIssuesDetailsWithCountMap[conversionIssue.ObjectType]
        if ok {
            conversionIssuesByType.Count = conversionIssuesByType.Count + 1
            conversionIssuesByType.SuggestionsErrors = append(
                conversionIssuesByType.SuggestionsErrors, conversionIssue)
        } else {
            var newConversionIssuesByType models.UnsupportedSqlWithDetails
            newConversionIssuesByType.Count = 1
            newConversionIssuesByType.UnsupportedType = conversionIssue.ObjectType
            newConversionIssuesByType.SuggestionsErrors =
                append(newConversionIssuesByType.SuggestionsErrors, conversionIssue)
            conversionIssuesDetailsWithCountMap[conversionIssue.ObjectType] =
                    newConversionIssuesByType
        }
    }
    fmt.Println(conversionIssuesDetailsWithCountMap)

    var conversionIssuesDetailsWithCountList []models.UnsupportedSqlWithDetails
    for _, value := range conversionIssuesDetailsWithCountMap {
        conversionIssuesDetailsWithCountList = append(conversionIssuesDetailsWithCountList, value)
    }
    fmt.Print(conversionIssuesDetailsWithCountList)

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

    assessmentReportVisualisationData := <- future

    if assessmentReportVisualisationData.Error != nil {
        return ctx.String(http.StatusInternalServerError,
            assessmentReportVisualisationData.Error.Error())
    }

    assessmentReport := assessmentReportVisualisationData.Report.AssessmentJsonReport

    voyagerAssessmentReportResponse := models.MigrationAssessmentReport{
        Summary:                models.AssessmentReportSummary{},
        SourceEnvironment:      models.SourceEnvironmentInfo{},
        SourceDatabase:         models.SourceDatabaseInfo{},
        TargetRecommendations:  models.TargetClusterRecommendationDetails{},
        RecommendedRefactoring: models.RecommendedRefactoringGraph{},
        UnsupportedDataTypes:   []models.UnsupportedSqlInfo{},
        UnsupportedFunctions:   []models.UnsupportedSqlInfo{},
        UnsupportedFeatures:    []models.UnsupportedSqlInfo{},
    }

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

    unsupportedDataTypeMap := make(map[string]int32)
    for _, unsupportedDataType := range assessmentReport.UnsupportedDataTypes {

        count, ok := unsupportedDataTypeMap[unsupportedDataType.DataType]
        if ok {

            unsupportedDataTypeMap[unsupportedDataType.DataType] = count + 1
        } else {
            unsupportedDataTypeMap[unsupportedDataType.DataType] = 1
        }
    }

    var unsupportedDataTypesList []models.UnsupportedSqlInfo
    for key, value := range unsupportedDataTypeMap {
        unsupportedSqlInfoTmp := models.UnsupportedSqlInfo{}
        unsupportedSqlInfoTmp.Count = value
        unsupportedSqlInfoTmp.UnsupportedType = key
        unsupportedDataTypesList = append(unsupportedDataTypesList, unsupportedSqlInfoTmp)
    }

    voyagerAssessmentReportResponse.UnsupportedDataTypes = unsupportedDataTypesList

    // Convert the backend model []UnsupportedFeatures into UX model []UnsupportedSqlInfo
    // to-do: Need to confirm with Voyager team on mapping
    var unsupportedFeaturesList []models.UnsupportedSqlInfo
    for _, unsupportedFeatureType := range assessmentReport.UnsupportedFeatures{
        unsupportedFeature := models.UnsupportedSqlInfo{}
        unsupportedFeature.UnsupportedType = unsupportedFeatureType.FeatureName
        unsupportedFeature.Count = int32(len(unsupportedFeatureType.Objects))
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
        row := conn.QueryRow(context.Background(), RETRIEVE_ASSESSMENT_REPORT, migrationUuid)
        err := row.Scan(&assessmentReportPayload)
        // log.Infof(fmt.Sprintf("assessment payload: [%s]", assessmentReportPayload))
        if err != nil {
            log.Errorf(fmt.Sprintf("[%s] Error while scaning results for query: [%s]",
                LOGGER_FILE_NAME, "RETRIEVE_ALL_VOYAGER_MIGRATIONS_SQL"))
            log.Errorf(err.Error())
            MigrationAsessmentReportResponse.Error = err
            future <- MigrationAsessmentReportResponse
            return
        }

        var assessmentVisualisationData helpers.AssessmentVisualisationMetadata
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
            return ctx.String(http.StatusInternalServerError,
                assessmentReportVisualisationData.Error.Error())
        }

        assessmentReport := assessmentReportVisualisationData.Report.AssessmentJsonReport

        sqlMetadataList := []models.SqlObjectMetadata{}
        tableIndexList := assessmentReport.TableIndexStats
        for _, dbObjectStat := range tableIndexList {
            var sqlMetadata1 models.SqlObjectMetadata
            sqlMetadata1.ObjectName = dbObjectStat.ObjectName
            if dbObjectStat.IsIndex {
                sqlMetadata1.SqlType = "Index"
            } else {
                sqlMetadata1.SqlType = "Table"
            }
            sqlMetadata1.RowCount = dbObjectStat.RowCount
            sqlMetadata1.Iops = dbObjectStat.Reads
            sqlMetadata1.Size = dbObjectStat.SizeInBytes
            sqlMetadataList = append(sqlMetadataList, sqlMetadata1)
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
            return ctx.String(http.StatusInternalServerError,
                assessmentReportVisualisationData.Error.Error())
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
