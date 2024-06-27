package handlers

import (
    "apiserver/cmd/server/helpers"
    "apiserver/cmd/server/logger"
    "apiserver/cmd/server/models"
    "context"
    "encoding/json"
    "fmt"
    "math/rand"
    "net/http"
    "strings"
    "time"
    "math"

    "github.com/jackc/pgtype"
    "github.com/jackc/pgx/v4/pgxpool"
    "github.com/labstack/echo/v4"
)

const LOGGER_FILE_NAME = "api_voyager"

const RETRIEVE_ALL_VOYAGER_MIGRATIONS_SQL string = "SELECT migration_uuid, " +
    "MAX(migration_phase) AS highest_migration_phase, " +
    "MAX(invocation_sequence) as highest_invocation_sequence " +
    "FROM ybvoyager_visualizer.ybvoyager_visualizer_metadata " +
    "GROUP BY migration_uuid"

const RETRIEVE_VOYGAGER_MIGRATION_DETAILS string = "SELECT database_name, schema_name, " +
    "status, invocation_timestamp, complexity, db_type " +
    "FROM ybvoyager_visualizer.ybvoyager_visualizer_metadata " +
    "WHERE migration_uuid=$1 AND migration_phase=$2 AND invocation_sequence=$3"

const RETRIEVE_ANALYZE_SCHEMA_PAYLOAD string = "SELECT payload " +
    "FROM ybvoyager_visualizer.ybvoyager_visualizer_metadata " +
    "WHERE migration_uuid=$1 AND migration_phase=$2 AND invocation_sequence=$3"

const RETRIEVE_MIGRATE_SCHEMA_PHASES_INFO string = "SELECT migration_UUID, " +
    "migration_phase, MAX(invocation_sequence) AS invocation_sequence " +
    "FROM ybvoyager_visualizer.ybvoyager_visualizer_metadata " +
    "WHERE migration_uuid=$1 AND migration_phase IN (0,1,3) " +
    "GROUP BY migration_uuid, migration_phase;"

const RETRIEVE_DATA_MIGRATION_METRICS string = "SELECT * FROM " +
    "ybvoyager_visualizer.ybvoyager_visualizer_table_metrics " +
    "WHERE migration_UUID=$1" +
    "ORDER BY schema_name"

const RETRIEVE_ASSESSMENT_REPORT string = "SELECT payload " +
    "FROM ybvoyager_visualizer.ybvoyager_visualizer_metadata " +
    "WHERE migration_UUID=$1 AND migration_phase=1 AND status='COMPLETED'"

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

type AllVoygaerMigrations struct {
    migrationUuid  string
    migrationPhase int
    invocationSeq  int
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

    var allVoygaerMigrationsList []AllVoygaerMigrations
    for rows.Next() {
        rowStruct := AllVoygaerMigrations{}
        err := rows.Scan(&rowStruct.migrationUuid, &rowStruct.migrationPhase,
            &rowStruct.invocationSeq)
        if err != nil {
            log.Errorf(fmt.Sprintf("[%s] Error while scaning results for query: [%s]",
                LOGGER_FILE_NAME, "RETRIEVE_ALL_VOYAGER_MIGRATIONS_SQL"))
            log.Errorf(err.Error())
            continue
        }
        allVoygaerMigrationsList = append(allVoygaerMigrationsList, rowStruct)
    }

    for _, allVoygaerMigration := range allVoygaerMigrationsList {

        var migrationPhaseToBeUsed int
        var migrationInvocationSqToBeUsed int
        if allVoygaerMigration.migrationPhase >= 1 {
            migrationPhaseToBeUsed = 1
            migrationInvocationSqToBeUsed = 2
        } else {
            migrationPhaseToBeUsed = allVoygaerMigration.migrationPhase
            migrationInvocationSqToBeUsed = allVoygaerMigration.invocationSeq
        }
        voyagerDetailsrows, err := conn.Query(context.Background(),
            RETRIEVE_VOYGAGER_MIGRATION_DETAILS,
            allVoygaerMigration.migrationUuid, migrationPhaseToBeUsed,
            migrationInvocationSqToBeUsed)
        if err != nil {
            log.Errorf(fmt.Sprintf("[%s] Error while querying for migration details",
                LOGGER_FILE_NAME))
            log.Errorf(err.Error())
            continue
        }

        for voyagerDetailsrows.Next() {
            rowStruct := models.VoyagerMigrationDetails{}
            var invocation_ts time.Time
            var complexity pgtype.Text

            rowStruct.MigrationUuid = allVoygaerMigration.migrationUuid
            rowStruct.MigrationPhase = int32(allVoygaerMigration.migrationPhase)
            rowStruct.InvocationSequence = int32(allVoygaerMigration.invocationSeq)
            err = voyagerDetailsrows.Scan(&rowStruct.DatabaseName, &rowStruct.SchemaName,
                &rowStruct.Status, &invocation_ts, &complexity, &rowStruct.SourceDb)
            if err != nil {
                log.Errorf(fmt.Sprintf("[%s] Error while scanning migration details",
                    LOGGER_FILE_NAME))
                log.Errorf(err.Error())
                continue
            }

            rowStruct.Status = "In Progress"
            if allVoygaerMigration.migrationPhase == 4 &&
                allVoygaerMigration.invocationSeq >= 2 {
                rowStruct.Status = "Complete"
                rowStruct.MigrationPhase = 5
            }

            rand.Seed(time.Now().UnixNano())
            rowStruct.InvocationTimestamp = invocation_ts.Format("2006-01-02 15:04:05")
            // rowStruct.MigrationName = "Migration_" + strconv.Itoa(rand.Intn(10002))
            rowStruct.MigrationName = "Migration_" +
                strings.Split(rowStruct.MigrationUuid, "-")[4]

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
            voyagerMigrationsResponse.Migrations = append(
                voyagerMigrationsResponse.Migrations, rowStruct)
        }
    }

    future <- voyagerMigrationsResponse
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
                }
                migrateSchemaTaskInfo.SuggestionsErrors = migrateSchemaUIDetails.SuggestionsErrors
                migrateSchemaTaskInfo.SqlObjects = migrateSchemaUIDetails.SqlObjects
                migrateSchemaTaskInfo.AnalyzeSchema = "complete"
            } else {
                migrateSchemaTaskInfo.ExportSchema = "in-progress"
            }
        case 3:
            if phaseInfo.invocationSeq%2 == 0 {
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
        RETRIEVE_VOYGAGER_MIGRATION_DETAILS, migrationUuid, 0, 2)
    if err != nil {
        c.logger.Errorf(fmt.Sprintf("[%s] Error while querying for assesment details",
            LOGGER_FILE_NAME))
        c.logger.Errorf(err.Error())
    }

    for voyagerDetailsrows.Next() {
        rowStruct := models.VoyagerMigrationDetails{}
        var invocation_ts time.Time
        var complexity pgtype.Text
        err = voyagerDetailsrows.Scan(&rowStruct.DatabaseName, &rowStruct.SchemaName,
            &rowStruct.Status, &invocation_ts, &complexity, &rowStruct.SourceDb)
        if err != nil {
            c.logger.Errorf(fmt.Sprintf("[%s] Error while scanning for assesment details",
                LOGGER_FILE_NAME))
            c.logger.Errorf(err.Error())
        }
        assesmentComplexity.Schema = rowStruct.SchemaName
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

    if assessmentReportVisualisationData.Error == nil {
        fmt.Println(assessmentReportVisualisationData.Report)
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
        int32(assessmentReportVisualisationData.Report.SourceSizeDetails.TotalTableSize)
    voyagerAssessmentReportResponse.SourceDatabase.TableRowCount =
        int32(assessmentReportVisualisationData.Report.SourceSizeDetails.TotalTableRowCount)
    voyagerAssessmentReportResponse.SourceDatabase.TotalTableSize =
        int32(assessmentReportVisualisationData.Report.SourceSizeDetails.TotalDBSize)
    voyagerAssessmentReportResponse.SourceDatabase.TotalIndexSize =
        int32(assessmentReportVisualisationData.Report.SourceSizeDetails.TotalIndexSize)


    voyagerAssessmentReportResponse.TargetRecommendations.RecommendationSummary =
        assessmentReport.Sizing.SizingRecommendation.ColocatedReasoning
    voyagerAssessmentReportResponse.TargetRecommendations.
        TargetClusterRecommendation.NumNodes =
        int32(assessmentReport.Sizing.SizingRecommendation.NumNodes)
    voyagerAssessmentReportResponse.TargetRecommendations.
        TargetClusterRecommendation.VcpuPerNode =
        int32(assessmentReport.Sizing.SizingRecommendation.VCPUsPerInstance)
    voyagerAssessmentReportResponse.TargetRecommendations.
        TargetClusterRecommendation.MemoryPerNode =
        int32(assessmentReport.Sizing.SizingRecommendation.MemoryPerInstance)
    voyagerAssessmentReportResponse.TargetRecommendations.
        TargetClusterRecommendation.InsertsPerNode =
        int32(assessmentReport.Sizing.SizingRecommendation.OptimalInsertConnectionsPerNode)
    voyagerAssessmentReportResponse.TargetRecommendations.
        TargetClusterRecommendation.ConnectionsPerNode =
        int32(assessmentReport.Sizing.SizingRecommendation.OptimalSelectConnectionsPerNode)

    voyagerAssessmentReportResponse.TargetRecommendations.
        TargetSchemaRecommendation.NoOfColocatedTables =
            int32(len(assessmentReport.Sizing.SizingRecommendation.ColocatedTables))
    voyagerAssessmentReportResponse.TargetRecommendations.
        TargetSchemaRecommendation.NoOfShardedTables =
            int32(len(assessmentReport.Sizing.SizingRecommendation.ShardedTables))
    voyagerAssessmentReportResponse.TargetRecommendations.
        TargetSchemaRecommendation.TotalSizeColocatedTables =
            int32(assessmentReportVisualisationData.Report.
                TargetSizingRecommendations.TotalColocatedSize)
    voyagerAssessmentReportResponse.TargetRecommendations.
        TargetSchemaRecommendation.TotalSizeShardedTables =
            int32(assessmentReportVisualisationData.Report.
                TargetSizingRecommendations.TotalShardedSize)

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

    for dbObjectType, count := range dbObjectConversionIssuesMap {
        _, ok := dbObjectsMap[dbObjectType]
        if ok {
            dbObjectsMap[dbObjectType] = dbObjectsMap[dbObjectType] - count
        }
    }

    voyagerAssessmentReportResponse.RecommendedRefactoring.Function.Automatic =
        int32(dbObjectsMap["FUNCTION"])
    voyagerAssessmentReportResponse.RecommendedRefactoring.Function.Manual =
        int32(dbObjectConversionIssuesMap["FUNCTION"])
    voyagerAssessmentReportResponse.RecommendedRefactoring.SqlType.Automatic =
        int32(dbObjectsMap["TYPE"])
    voyagerAssessmentReportResponse.RecommendedRefactoring.SqlType.Manual =
        int32(dbObjectConversionIssuesMap["TYPE"])
    voyagerAssessmentReportResponse.RecommendedRefactoring.Table.Automatic =
        int32(dbObjectsMap["TABLE"])
    voyagerAssessmentReportResponse.RecommendedRefactoring.Table.Manual =
        int32(dbObjectConversionIssuesMap["TABLE"])
    voyagerAssessmentReportResponse.RecommendedRefactoring.Triggers.Automatic =
        int32(dbObjectsMap["TRIGGER"])
    voyagerAssessmentReportResponse.RecommendedRefactoring.Triggers.Manual =
        int32(dbObjectConversionIssuesMap["TRIGGER"])
    voyagerAssessmentReportResponse.RecommendedRefactoring.View.Automatic =
        int32(dbObjectsMap["VIEW"])
    voyagerAssessmentReportResponse.RecommendedRefactoring.View.Manual =
        int32(dbObjectConversionIssuesMap["VIEW"])

    // Convert the backend model []UnsupportedDataTypes into UX model []UnsupportedSqlInfo
    unsupportedDataTypeMap := make(map[string]models.UnsupportedSqlInfo)
    for _, unsupportedDataType := range assessmentReport.UnsupportedDataTypes {

        unsupportedSqlInfo, ok := unsupportedDataTypeMap[unsupportedDataType.DataType]
        if ok {
            unsupportedSqlInfo.Count++
            unsupportedDataTypeMap[unsupportedDataType.DataType] = unsupportedSqlInfo
        } else {
            unsupportedSqlInfoTmp := models.UnsupportedSqlInfo{}
            unsupportedSqlInfoTmp.Count = 1
            unsupportedSqlInfoTmp.UnsupportedType = unsupportedDataType.DataType
            unsupportedDataTypeMap[unsupportedDataType.DataType] = models.UnsupportedSqlInfo{}
        }
    }
    var unsupportedDataTypesList []models.UnsupportedSqlInfo
    for _, value := range unsupportedDataTypeMap {
        unsupportedDataTypesList = append(unsupportedDataTypesList, value)
    }
    voyagerAssessmentReportResponse.UnsupportedDataTypes = unsupportedDataTypesList

    // Convert the backend model []UnsupportedFeatures into UX model []UnsupportedSqlInfo
    // to-do: Need to confirm with Voyager team on mapping
    var unsupportedFeaturesList []models.UnsupportedSqlInfo
    for _, unsupportedFeatureType := range assessmentReport.UnsupportedFeatures{
        unsupportedFeature := models.UnsupportedSqlInfo{}
        unsupportedFeature.UnsupportedType = unsupportedFeatureType.FeatureName
        unsupportedFeature.Count = int32(len(unsupportedFeatureType.ObjectNames))
        if (unsupportedFeature.Count == 0) {
            unsupportedFeature.Count = 1
        }
        unsupportedFeaturesList = append(unsupportedFeaturesList, unsupportedFeature)
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
        log.Infof(fmt.Sprintf("assessment payload: [%s]", assessmentReportPayload))
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
