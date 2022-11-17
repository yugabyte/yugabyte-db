package helpers

import (
    "fmt"
    "io/ioutil"
    "net/http"
    "regexp"
    "strings"
    "time"
)

type TabletInfo struct {
    Namespace  string
    TableName  string
    TableUuid  string
    State      string
    HasLeader  bool
}

// Tablets maps tablet ID to tablet info
type TabletsFuture struct {
    Tablets map[string]TabletInfo
    Error   error
}

// TODO: replace this with a call to a json endpoint so we don't have to parse html
func parseTabletsFromHtml(body string) (map[string]TabletInfo, error) {
    tablets := map[string]TabletInfo{}
    // Regex for getting the table from html
    tableRegex, err := regexp.Compile(`(?ms)<table class='table table-striped'>(.*?)<\/table>`)
    if err != nil {
        return tablets, err
    }
    rowRegex, err := regexp.Compile(
        `(?ms)<tr><td>(.*?)</td><td>(.*?)</td><td>(.*?)</td><td>(.*?)</td><td>(.*?)</td>`+
        `<td>(.*?)</td><td>(.*?)</td><td>(.*?)</td><td>(.*?)</td><td>(.*?)</td>`)
    if err != nil {
        return tablets, err
    }
    tableHtml := tableRegex.FindString(body)

    rowMatches := rowRegex.FindAllStringSubmatch(tableHtml, -1)
    if err != nil {
        return tablets, err
    }
    linkRegex, err := regexp.Compile(`(?ms)<a.*?>(.*?)</a>`)
    if err != nil {
        return tablets, err
    }
    for _, row := range rowMatches {
        namespace := row[1]
        tableName := row[2]
        tableUuid := row[3]
        tabletId := linkRegex.FindStringSubmatch(row[4])[1]
        state := row[6]
        raftConfig := row[10]
        hasLeader := strings.Contains(raftConfig, "LEADER")
        tablets[tabletId] = TabletInfo{
            Namespace: namespace,
            TableName: tableName,
            TableUuid: tableUuid,
            State: state,
            HasLeader: hasLeader,
        }
    }
    return tablets, nil
}

func GetTabletsFuture(nodeHost string, future chan TabletsFuture) {
    tablets := TabletsFuture{
        Tablets: map[string]TabletInfo{},
        Error: nil,
    }
    httpClient := &http.Client{
        Timeout: time.Second * 10,
    }
    url := fmt.Sprintf("http://%s:9000/tablets", nodeHost)
    resp, err := httpClient.Get(url)
    if err != nil {
        tablets.Error = err
        future <- tablets
        return
    }
    defer resp.Body.Close()
    body, err := ioutil.ReadAll(resp.Body)
    if err != nil {
        tablets.Error = err
        future <- tablets
        return
    }
    tablets.Tablets, tablets.Error = parseTabletsFromHtml(string(body))
    future <- tablets
}
