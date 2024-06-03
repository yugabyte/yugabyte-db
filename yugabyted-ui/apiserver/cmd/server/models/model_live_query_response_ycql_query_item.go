package models

// LiveQueryResponseYcqlQueryItem - Schema for Live Query Response YCQL Query Item
type LiveQueryResponseYcqlQueryItem struct {

    Id string `json:"id"`

    NodeName string `json:"node_name"`

    Keyspace string `json:"keyspace"`

    Query string `json:"query"`

    Type string `json:"type"`

    ElapsedMillis int64 `json:"elapsed_millis"`

    ClientHost string `json:"client_host"`

    ClientPort string `json:"client_port"`
}
