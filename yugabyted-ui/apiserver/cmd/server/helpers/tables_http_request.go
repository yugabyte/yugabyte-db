package helpers

import (
    "errors"
    "fmt"
    "io/ioutil"
    "net/http"
    "regexp"
    "strconv"
    "time"
)

type Table struct {
    Keyspace string
    Name string
    SizeBytes int64
    IsYsql bool
}

type TablesFuture struct {
    Tables []Table
    Error error
}

func getBytesFromString(sizeString string) (int64, error) {
    // Possible units are BKMGTPE, for byte, kilobyte, megabyte, etc.
    if len(sizeString) < 1 {
        return 0, nil
    }
    unit := string([]rune(sizeString)[len(sizeString) - 1])
    valString := string([]rune(sizeString)[0:len(sizeString)-1])
    conversionFactor := int64(0)
    switch unit {
    case "B":
        conversionFactor = 1
    case "K":
        conversionFactor = 1024
    case "M":
        conversionFactor = 1024 * 1024
    case "G":
        conversionFactor = 1024 * 1024 * 1024
    case "T":
        conversionFactor = 1024 * 1024 * 1024 * 1024
    case "P":
        conversionFactor = 1024 * 1024 * 1024 * 1024 * 1024
    case "E":
        conversionFactor = 1024 * 1024 * 1024 * 1024 * 1024 * 1024
    default:
        return 0, errors.New("could not find unit for table size")
    }
    val, err := strconv.ParseFloat(valString, 64)
    if err != nil {
        return 0, err
    }
    byteVal := int64(int64(val) * conversionFactor)
    return byteVal, nil
}

func parseTablesFromHtml(body string) ([]Table, error) {
    tables := []Table{}
    // Regex for getting table of User tables and Index tables from html
    userTablesRegex, err := regexp.Compile(`(?ms)User tables</h2></div>.*?</div>`)
    if err != nil {
        return tables, err
    }
    indexTablesRegex, err := regexp.Compile(`(?ms)Index tables</h2></div>.*?</div>`)
    if err != nil {
        return tables, err
    }
    userTablesHtml := userTablesRegex.FindString(body)
    indexTablesHtml := indexTablesRegex.FindString(body)
    rowRegex, err := regexp.Compile(`<tr>.*?</tr>`)
    if err != nil {
        return tables, err
    }
    userTableRowMatches := rowRegex.FindAllString(userTablesHtml, -1)
    indexTableRowMatches := rowRegex.FindAllString(indexTablesHtml, -1)
    dataRegex, err := regexp.Compile(`<td>(.*?)</td><td><a.*?>(.*?)</a></td><td>.*?</td>`+
        `<td>.*?</td><td>.*?</td><td>(.*?)</td>(.*?Total:\s*(.*?)<li>)?`)
    if err != nil {
        return tables, err
    }
    // For each match, group 1 is keyspace, group 2 is table name,
    // group 3 is the YSQL OID (to distinguish between YSQL and YCQL tables) and
    // group 5 is total size as a string
    for _, row := range append(userTableRowMatches, indexTableRowMatches...) {
        data := dataRegex.FindStringSubmatch(row)
        sizeBytes, err := getBytesFromString(data[5])
        if err != nil {
            return tables, err
        }
        tables = append(tables, Table{
            Keyspace: data[1],
            Name: data[2],
            SizeBytes: sizeBytes,
            IsYsql: data[3] != "",
        })
    }
    return tables, nil
}

func GetTablesFuture(nodeHost string, future chan TablesFuture) {
    tables := TablesFuture{
        Tables: []Table{},
        Error: nil,
    }
    httpClient := &http.Client{
        Timeout: time.Second * 10,
    }
    url := fmt.Sprintf("http://%s:7000/tables", nodeHost)
    resp, err := httpClient.Get(url)
    if err != nil {
        tables.Error = err
        future <- tables
        return
    }
    defer resp.Body.Close()
    body, err := ioutil.ReadAll(resp.Body)
    if err != nil {
        tables.Error = err
        future <- tables
        return
    }
    tables.Tables, tables.Error = parseTablesFromHtml(string(body))
    future <- tables
}
