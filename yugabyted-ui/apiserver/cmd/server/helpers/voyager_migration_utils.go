package helpers

import (
    "apiserver/cmd/server/logger"
    "context"
    "encoding/json"
    "fmt"
    "strings"

    "github.com/jackc/pgx/v4/pgxpool"
    "github.com/yugabyte/yb-voyager/yb-voyager/src/ybversion"
)

const LOGGER_FILE_NAME = "voyager_migration_utils"

const UPDATE_COMPLEXITY = "UPDATE ybvoyager_visualizer.ybvoyager_visualizer_metadata " +
    "SET complexity=$1 WHERE migration_uuid=$2 AND migration_phase=0 AND invocation_sequence=2"

const RETRIVE_PAYLOAD_SQL = "SELECT payload " +
    "FROM ybvoyager_visualizer.ybvoyager_visualizer_metadata " +
    "WHERE migration_uuid=$1 AND migration_phase=$2 AND invocation_sequence=$3"

type DatabaseObject struct {
    ObjectType   string `json:"objectType"`
    TotalCount   int    `json:"totalCount"`
    InvalidCount int    `json:"invalidCount"`
    ObjectNames  string `json:"objectNames"`
    Details      string `json:"details"`
}

type SqlIssue struct {
    ObjectType   string `json:"objectType"`
    ObjectName   string `json:"objectName"`
    Reason       string `json:"reason"`
    SqlStatement string `json:"sqlStatement"`
    FilePath     string `json:"filePath"`
    Suggestion   string `json:"suggestion"`
    GithubIssue  string `json:"GH"`
}

type SchemaAnalyzeReport struct {
    DbName          string           `json:"dbName"`
    SchemaName      string           `json:"schemaName"`
    DbVersion       string           `json:"dbVersion"`
    Notes           string           `json:"notes"`
    DatabaseObjects []DatabaseObject `json:"databaseObjects"`
    Issues          []SqlIssue       `json:"issues"`
}

type AssessmentVisualisationMetadata struct {
    PayloadVersion                  string                       `json:"PayloadVersion"`
    VoyagerVersion                  string                       `json:"VoyagerVersion"`
    TargetDbVersion                 string                       `json:"TargetDBVersion"`
    MigrationComplexity             string                       `json:"MigrationComplexity"`
    MigrationComplexityExplanation  string                  `json:"MigrationComplexityExplanation"`
    SchemaSummary                   SchemaSummary                `json:"SchemaSummary"`
    AssessmentIssues                []VoyagerAssessmentIssueInfo `json:"AssessmentIssues"`
    SourceSizeDetails               SourceDBSizeDetails          `json:"SourceSizeDetails"`
    TargetRecommendations           TargetSizingRecommendations  `json:"TargetRecommendations"`
    ConversionIssues                []Issue                      `json:"ConversionIssues"`

    // Deprecated: AssessmentJsonReport is retained for backward compatibility.
    AssessmentJsonReport AssessmentReport `json:"AssessmentJsonReport"`
}

type AssessmentReport struct {
    VoyagerVersion             string                      `json:"VoyagerVersion"`
    MigrationComplexity        string                      `json:"MigrationComplexity"`
    SchemaSummary              SchemaSummary               `json:"SchemaSummary"`
    Sizing                     SizingAssessmentReport      `json:"Sizing"`
    UnsupportedDataTypes       []TableColumnsDataTypes     `json:"UnsupportedDataTypes"`
    UnsupportedDataTypesDesc   string                      `json:"UnsupportedDataTypesDesc"`
    UnsupportedFeatures        []UnsupportedFeature        `json:"UnsupportedFeatures"`
    UnsupportedFeaturesDesc    string                      `json:"UnsupportedFeaturesDesc"`
    UnsupportedQueryConstructs []UnsupportedQueryConstruct `json:"UnsupportedQueryConstructs"`
    MigrationCaveats           []UnsupportedFeature        `json:"MigrationCaveats"`
    TableIndexStats            []TableIndexStats           `json:"TableIndexStats"`
    Notes                      []string                    `json:"Notes"`
}

type VoyagerAssessmentIssueInfo struct {
    Category               string                          `json:"Category"`
    CategoryDescription    string                          `json:"CategoryDescription"`
    Type                   string                          `json:"Type"`
    Name                   string                          `json:"Name"`
    Description            string                          `json:"Description"`
    Impact                 string                          `json:"Impact"`
    ObjectType             string                          `json:"ObjectType"`
    ObjectName             string                          `json:"ObjectName"`
    SqlStatement           string                          `json:"SqlStatement"`
    DocsLink               string                          `json:"DocsLink"`
    MinimumVersionsFixedIn map[string]*ybversion.YBVersion `json:"MinimumVersionsFixedIn"`

    // Store Type-specific details - extensible, can refer any struct
    // Details json.RawMessage `json:"Details,omitempty"`
}

type SourceDBSizeDetails struct {
    TotalDBSize        int64    `json:"TotalDBSize"`
    TotalTableSize     int64    `json:"TotalTableSize"`
    TotalIndexSize     int64    `json:"TotalIndexSize"`
    TotalTableRowCount int64    `json:"TotalTableRowCount"`
}

type TargetSizingRecommendations struct {
    TotalColocatedSize int64    `json:"TotalColocatedSize"`
    TotalShardedSize   int64    `json:"TotalShardedSize"`
}

type SchemaSummary struct {
    DBName      string     `json:"DbName,omitempty"`
    SchemaNames []string   `json:"SchemaNames,omitempty"`
    DBVersion   string     `json:"DbVersion,omitempty"`
    Notes       []string   `json:"Notes,omitempty"`
    DBObjects   []DBObject `json:"DatabaseObjects"`
}

type DBObject struct {
    ObjectType   string `json:"ObjectType"`
    TotalCount   int    `json:"TotalCount"`
    InvalidCount int    `json:"InvalidCount,omitempty"`
    ObjectNames  string `json:"ObjectNames,omitempty"`
    Details      string `json:"Details,omitempty"`
}

type SizingAssessmentReport struct {
    SizingRecommendation SizingRecommendation `json:"SizingRecommendation"`
    FailureReasoning     string               `json:"FailureReasoning"`
}

type SizingRecommendation struct {
    ColocatedTables                 []string    `json:"ColocatedTables"`
    ColocatedReasoning              string      `json:"ColocatedReasoning"`
    ShardedTables                   []string    `json:"ShardedTables"`
    NumNodes                        float64     `json:"NumNodes"`
    VCPUsPerInstance                int         `json:"VCPUsPerInstance"`
    MemoryPerInstance               int         `json:"MemoryPerInstance"`
    OptimalSelectConnectionsPerNode int64       `json:"OptimalSelectConnectionsPerNode"`
    OptimalInsertConnectionsPerNode int64       `json:"OptimalInsertConnectionsPerNode"`
    EstimatedTimeInMinForImport     float64     `json:"EstimatedTimeInMinForImport"`
    ParallelVoyagerJobs             float64     `json:"ParallelVoyagerJobs"`
}

type TableColumnsDataTypes struct {
    SchemaName string `json:"SchemaName"`
    TableName  string `json:"TableName"`
    ColumnName string `json:"ColumnName"`
    DataType   string `json:"DataType"`
}

type UnsupportedFeature struct {
    FeatureName string   `json:"FeatureName"`
    // ObjectNames []string `json:"ObjectNames"`
    Objects            []ObjectInfo `json:"Objects"`
    DocsLink           string       `json:"DocsLink,omitempty"`
}

type UnsupportedQueryConstruct struct {
    ConstructTypeName      string
    Query                  string
    DocsLink               string
    MinimumVersionsFixedIn map[string]*ybversion.YBVersion // key: series (2024.1, 2.21, etc)
}

type ObjectInfo struct {
    ObjectName   string
    SqlStatement string
}

type TableIndexStats struct {
    SchemaName      string  `json:"SchemaName"`
    ObjectName      string  `json:"ObjectName"`
    RowCount        int64  `json:"RowCount"` // Pointer to allows null values
    ColumnCount     int64  `json:"ColumnCount"`
    Reads           int64  `json:"Reads"`
    Writes          int64  `json:"Writes"`
    ReadsPerSecond  int64  `json:"ReadsPerSecond"`
    WritesPerSecond int64  `json:"WritesPerSecond"`
    IsIndex         bool    `json:"IsIndex"`
    ObjectType      string  `json:"ObjectType"`
    ParentTableName string `json:"ParentTableName"`
    SizeInBytes     int64  `json:"SizeInBytes"`
}

type Issue struct {
    ObjectType   string `json:"ObjectType"`
    ObjectName   string `json:"ObjectName"`
    Reason       string `json:"Reason"`
    SqlStatement string `json:"SqlStatement,omitempty"`
    FilePath     string `json:"FilePath"`
    Suggestion   string `json:"Suggestion"`
    GH           string `json:"GH"`
}

func CalculateAndUpdateComplexity(log logger.Logger, pgClient *pgxpool.Pool,
    migrationUuid string, migrationPhase int, migrationInvocationSq int) {

    var schemaAnalyzeReport SchemaAnalyzeReport
    var complexity string
    var payload string

    rows, err := pgClient.Query(context.Background(), RETRIVE_PAYLOAD_SQL, migrationUuid,
        migrationPhase, migrationInvocationSq)
    if err != nil {
        log.Errorf(fmt.Sprintf("[%s] Error fetching payload", LOGGER_FILE_NAME))
        log.Errorf(err.Error())
    }
    defer rows.Close()

    for rows.Next() {
        err := rows.Scan(&payload)
        if err != nil {
            log.Errorf(fmt.Sprintf("[%s] Error during row scan", LOGGER_FILE_NAME))
            log.Errorf(err.Error())
        }
    }

    err = json.Unmarshal([]byte(payload), &schemaAnalyzeReport)
    if err != nil {
        log.Errorf(fmt.Sprintf("[%s] Error parsing schema analysis report",
            LOGGER_FILE_NAME))
        log.Errorf(err.Error())
        return
    }

    complexity = calculateComplexity(schemaAnalyzeReport.DatabaseObjects,
        schemaAnalyzeReport.Issues)
    _, err = pgClient.Exec(context.Background(), UPDATE_COMPLEXITY, complexity, migrationUuid)
    if err != nil {
        log.Errorf(fmt.Sprintf("[%s] Error updating the complexity for [%s]",
            LOGGER_FILE_NAME, migrationUuid))
        log.Errorf(err.Error())
        return
    }
    log.Infof(fmt.Sprintf("[%s] Calculated complexity for migration UUID %s: %s",
        LOGGER_FILE_NAME, migrationUuid, complexity))
}

func calculateComplexity(databaseObjectList []DatabaseObject, issuesList []SqlIssue) string {
    var totalComplexity int
    totalComplexity = 0
    var totalSqlObjectCount int

    for index := range databaseObjectList {
        totalSqlObjectCount += databaseObjectList[index].TotalCount
    }

    if totalSqlObjectCount > 0 && totalSqlObjectCount < 20 {
        totalComplexity += 1
    } else if totalSqlObjectCount >= 20 && totalSqlObjectCount < 50 {
        totalComplexity += 2
    } else if totalSqlObjectCount > 50 {
        totalComplexity += 3
    }

    issuesCount := len(issuesList)
    if issuesCount > 0 && issuesCount < 5 {
        totalComplexity += 1
    } else if issuesCount >= 5 && issuesCount < 10 {
        totalComplexity += 2
    } else if issuesCount >= 10 {
        totalComplexity += 3
    }

    for index := range issuesList {
        githubIssue := issuesList[index].GithubIssue
        if !isBlank(githubIssue) {
            totalComplexity += 2
        }
    }

    var complexity string
    if totalComplexity >= 0 && totalComplexity <= 4 {
        complexity = "LOW"
    } else if totalComplexity > 5 && totalComplexity <= 10 {
        complexity = "MEDIUM"
    } else {
        complexity = "HIGH"
    }
    return complexity
}

func isBlank(str string) bool {
    return len(strings.TrimSpace(str)) == 0
}
