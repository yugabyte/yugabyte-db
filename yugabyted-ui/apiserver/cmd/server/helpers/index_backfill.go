package helpers

import (
    "context"
    "fmt"
    "net/url"
    "regexp"

    "github.com/jackc/pgx/v4/pgxpool"
)

const QUERY_INDEX_BACKFILL_PROGRESS_INFO = "SELECT * FROM pg_stat_progress_create_index;"

type IndexBackfillInfoStruct struct {
    PId              int         `json:"pid"`
    DatId            uint32      `json:"datid"`
    DatName          string      `json:"datname"`
    RelId            uint32      `json:"relid"`
    IndexRelId       uint32      `json:"index_relid"`
    Command          string      `json:"command"`
    Phase            string      `json:"phase"`
    LockersTotal     interface{} `json:"lockers_total"`
    LockersDone      interface{} `json:"lockers_done"`
    CurrentLockerPId interface{} `json:"current_locker_pid"`
    BlocksTotal      interface{} `json:"blocks_total"`
    BlocksDone       interface{} `json:"blocks_done"`
    TuplesTotal      interface{} `json:"tuples_total"`
    TuplesDone       interface{} `json:"tuples_done"`
    PartitionsTotal  interface{} `json:"partitions_total"`
    PartitionsDone   interface{} `json:"partitions_done"`
}

type IndexBackFillInfoFuture struct {
    IndexBackFillInfo []map[string]interface{}
    Error             error
}

type IndexTaskStruct struct {
    StartTime string
    Duration  string
    IndexName string
}

type IndexInfoStruct struct {
    RelId            uint32      `json:"relid"`
    IndexRelId       uint32      `json:"indexrelid"`
    SchemaName       string      `json:"schemaname"`
    RelName          string      `json:"relname"`
    IndexRelName     string      `json:"indexrelname"`
    IdxScan          int         `json:"idx_scan"`
    IdxTupRead       int         `json:"idx_tup_read"`
    IdxTupFetch      int         `json:"idx_tup_fetch"`
    Error            error
}

// TODO: replace this with a call to a json endpoint so we don't have to parse html
func (h *HelperContainer) ParseTasksFromHtml(body string) ([]IndexTaskStruct, error) {
    indexTasks := []IndexTaskStruct{}

    tasksRegex, err := regexp.Compile(`(?ms)Last 20 user-initiated jobs started` +
        ` in the past 24 hours</h3>.*?</table>`)
    if err != nil {
        return indexTasks, err
    }
    completedTasks := tasksRegex.FindString(body)

    rowRegex, err := regexp.Compile(`<tr>.*?</tr>`)
    if err != nil {
        return indexTasks, err
    }
    completedTasksMatches := rowRegex.FindAllString(completedTasks, -1)

    dataRegex, err := regexp.Compile(`<th>(.*?)</th><td>(.*?)</td><td>(.*?)</td>` +
        `<td>(.*?)</td><td>(.*?)</td>`)
    if err != nil {
        return indexTasks, err
    }

    indexNameRegex, err := regexp.Compile(`Backfilling { (.*?) } Done`)
    if err != nil {
        return indexTasks, err
    }
    // For each match, group 1 is task name, group 2 is status,
    // group 3 is the start time, group 4 is the duration of task
    // group 5 is description which has the index name
    for _, row := range completedTasksMatches {
        data := dataRegex.FindStringSubmatch(row)
        if len(data) == 6 {
            if data[1] == "Backfill Table" && data[2] == "kComplete" {
                indexName := indexNameRegex.FindStringSubmatch(data[5])[1]

                indexTasks = append(indexTasks, IndexTaskStruct{
                    StartTime: data[3],
                    Duration: data[4],
                    IndexName: indexName,
                })
            }
        }
    }
    return indexTasks, nil
}

func (h *HelperContainer) GetCompletedIndexBackFillInfo() IndexBackFillInfoFuture {
    completedIndexBackFillInfoFuture := IndexBackFillInfoFuture{
        IndexBackFillInfo: []map[string]interface{}{},
        Error:             nil,
    }
    body, err := h.BuildMasterURLsAndAttemptGetRequests(
        "tasks", // path
        url.Values{}, // params
        false, // expectJson
    )
    if err != nil {
        completedIndexBackFillInfoFuture.Error = err
        return completedIndexBackFillInfoFuture
    }

    backfilledIndexes, err := h.ParseTasksFromHtml(string(body))
    if err != nil {
        completedIndexBackFillInfoFuture.Error = err
        return completedIndexBackFillInfoFuture
    }

    for _, backfilledIndex := range backfilledIndexes {
        completedIndexBackFillInfoFuture.IndexBackFillInfo = append(
            completedIndexBackFillInfoFuture.IndexBackFillInfo, map[string]interface{}{
                "IndexName": backfilledIndex.IndexName,
                "StartTime": backfilledIndex.StartTime,
                "Duration":  backfilledIndex.Duration,
                "Phase":     "Completed",
            })
    }

    return completedIndexBackFillInfoFuture
}

func (h *HelperContainer) GetIndexInfo(
    pgClient *pgxpool.Pool,
    indexRelId uint32,
    future chan IndexInfoStruct,
) {
    indexInfo := IndexInfoStruct{
        Error: nil,
    }

    query := fmt.Sprintf("SELECT * from pg_stat_all_indexes WHERE indexrelid = %d;", indexRelId)
    rows, err := pgClient.Query(context.Background(), query)
    if err != nil {
        indexInfo.Error = err
        future <- indexInfo
        return
    }
    defer rows.Close()

    for rows.Next() {
        err := rows.Scan(&indexInfo.RelId, &indexInfo.IndexRelId,
            &indexInfo.SchemaName, &indexInfo.RelName,
            &indexInfo.IndexRelName, &indexInfo.IdxScan,
            &indexInfo.IdxTupRead, &indexInfo.IdxTupFetch)

        if err != nil {
            indexInfo.Error = err
            future <- indexInfo
            return
        }
    }
    err = rows.Err()
    if err != nil {
        indexInfo.Error = err
        future <- indexInfo
        return
    }
    future <- indexInfo
}

func (h *HelperContainer) convertInterfaceToInt64(val interface{}) int64 {
    if val == nil {
        return 0
    } else {
        return val.(int64)
    }
}

func (h *HelperContainer) GetIndexBackFillInfo(
    pgClient *pgxpool.Pool,
    future chan IndexBackFillInfoFuture,
) {
    indexBackFillInfoFuture := IndexBackFillInfoFuture{
        IndexBackFillInfo: []map[string]interface{}{},
        Error:             nil,
    }

    rows, err := pgClient.Query(context.Background(), QUERY_INDEX_BACKFILL_PROGRESS_INFO)
    if err != nil {indexBackFillInfoFuture.Error = err
        future <- indexBackFillInfoFuture
        return
    }
    defer rows.Close()

    indexInfoFutures := []chan IndexInfoStruct{}
    indexRelIdtoBackFillInfoMap := make(map[uint32] IndexBackfillInfoStruct)

    for rows.Next() {
        indexBackFillInfo := IndexBackfillInfoStruct{}
        err := rows.Scan(&indexBackFillInfo.PId, &indexBackFillInfo.DatId,
            &indexBackFillInfo.DatName, &indexBackFillInfo.RelId,
            &indexBackFillInfo.IndexRelId, &indexBackFillInfo.Command,
            &indexBackFillInfo.Phase, &indexBackFillInfo.LockersTotal,
            &indexBackFillInfo.LockersDone, &indexBackFillInfo.CurrentLockerPId,
            &indexBackFillInfo.BlocksTotal, &indexBackFillInfo.BlocksDone,
            &indexBackFillInfo.TuplesTotal, &indexBackFillInfo.TuplesDone,
            &indexBackFillInfo.PartitionsTotal, &indexBackFillInfo.PartitionsDone)

        if err != nil {
            indexBackFillInfoFuture.Error = err
            future <- indexBackFillInfoFuture
            return
        }

        if indexBackFillInfo.Phase == "backfilling" {
            indexInfoFuture := make(chan IndexInfoStruct)
            indexInfoFutures = append(indexInfoFutures, indexInfoFuture)
            if err != nil {
                indexBackFillInfoFuture.Error = err
                future <- indexBackFillInfoFuture
                return
            }
            go h.GetIndexInfo(pgClient, indexBackFillInfo.IndexRelId, indexInfoFuture)
            indexRelIdtoBackFillInfoMap[indexBackFillInfo.IndexRelId] = indexBackFillInfo
        }

        indexRelIdtoBackFillInfoMap[indexBackFillInfo.IndexRelId] =
            indexBackFillInfo
    }
    err = rows.Err()
    if err != nil {
        indexBackFillInfoFuture.Error = err
        future <- indexBackFillInfoFuture
        return
    }

    for _, indexInfoFuture := range indexInfoFutures {
        indexInfo := <- indexInfoFuture
        if indexInfo.Error != nil {
            indexBackFillInfoFuture.Error = err
            future <- indexBackFillInfoFuture
            return
        }
        indexBackFillInfo := indexRelIdtoBackFillInfoMap[indexInfo.IndexRelId]
        indexBackFillInfo.TuplesTotal = h.convertInterfaceToInt64(indexBackFillInfo.TuplesTotal)
        indexBackFillInfo.TuplesDone = h.convertInterfaceToInt64(indexBackFillInfo.TuplesDone)
        indexBackFillInfo.PartitionsTotal = h.convertInterfaceToInt64(
            indexBackFillInfo.PartitionsTotal)
        indexBackFillInfo.PartitionsDone = h.convertInterfaceToInt64(
            indexBackFillInfo.PartitionsDone)


        indexBackFillInfoFuture.IndexBackFillInfo = append(
            indexBackFillInfoFuture.IndexBackFillInfo, map[string]interface{}{
                "IndexName":        indexInfo.IndexRelName,
                "DBName":           indexBackFillInfo.DatName,
                "Command":          indexBackFillInfo.Command,
                "Phase":            indexBackFillInfo.Phase,
                "TuplesTotal":      indexBackFillInfo.TuplesTotal,
                "TuplesDone":       indexBackFillInfo.TuplesDone,
                "PartitionsTotal":  indexBackFillInfo.PartitionsTotal,
                "PartitionsDone":   indexBackFillInfo.PartitionsDone,
            })
    }

    future <- indexBackFillInfoFuture
}
