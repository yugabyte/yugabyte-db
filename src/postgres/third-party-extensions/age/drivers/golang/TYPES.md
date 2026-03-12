# Apache AGE - Go driver Type mapping

* For more information about Apache AGE result types : https://age.apache.org/docs/Apache_AGE_Guide.pdf

| Type | AGE Result | Go Type |
|------|------------|---------|
|Vertex|::vertex    |Vertex<br> vertex.Id() int64<br>  vertex.Label() string<br>vertex.Prop(string) interface{} |
|Edge  |::edge      |Edge<br>edge.Id() int64<br>edge.Label() string<br>edge.StartId() int64<br>edge.EndId() int64<br>edge.Prop(string) interface{}          |
|Path  |::path      |Path<br>path.Size() int // quantity of vertices and edges in this path<br>path.Get(index int) Entity // *Vertex or *Edge<br>path.GetAsVertex(index int) *Vertex<br>path.GetAsEdge(index int) *Edge          |
|Integer |int       |int64    |
|Float |float<br>NaN, -Infinity, Infinity |float64 <br>math.Nan(), math.Inf(-1),math.Inf(1)   |
|Numeric |::numeric |*big.Int<br>*big.Float |
|String|string       |string     |
|Boolean|bool       |bool     |
|Null|empty result |nil         |

