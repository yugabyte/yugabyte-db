package helpers

import (
    "fmt"
    "io/ioutil"
    "net/http"
    "regexp"
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

// TODO: replace this with a call to a json endpoint so we don't have to parse html
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
        sizeBytes, err := GetBytesFromString(data[5])
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
