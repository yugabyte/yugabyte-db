import { Aggregation, AnomalyCategory, AnomalyType, SplitMode, SplitType } from "../helpers/dtos";

export const ANOMALY_DATA_LIST = [
  {
    metadataUuid: "ffac2c96-2db6-401d-a2e0-4f5767962cc2",
    uuid: "e84f9ee1-2a88-4fcd-94b8-eed47e855579",
    category: AnomalyCategory.SQL,
    type: AnomalyType.SQL_QUERY_LATENCY_INCREASE,
    universeUuid: "c623b9af-e863-4b0b-b98c-7bdcff465c2b",
    affectedNodes: [
      {
        name: "yb-15-troubleshooting-service-test-n1",
        uuid:"b738423c-7176-44f7-8e78-0085542ece0d"
      },
      {
        name: "yb-15-troubleshooting-service-test-n2",
        uuid:"61b2e298-ae29-4170-a0da-135c67f83853"
      }
    ],
    affectedTables: [],
    title: "SQL query latency increase detected",
    summary: "P95 latency increased for query 'SELECT * FROM test_table WHERE field = $1'",
    detectionTime: "2024-01-19T13:07:18Z",
    startTime: "2024-01-18T15:00:00Z",
    endTime: "2024-01-18T15:30:00Z",
    graphStartTime: "2024-01-18T14:00:00Z",
    graphEndTime: "2024-01-18T16:30:00Z",
    mainGraphs: [
      {
        name: "query_latency",
        filters: {
          universeUuid: ["c623b9af-e863-4b0b-b98c-7bdcff465c2b"],
          "queryId": ["3694949039461716331"],
          "dbId": ["16384"]
        }
      }
    ],
    supportingGraphs: [
      {
        name: "query_rps",
        filters: {
          universeUuid: ["c623b9af-e863-4b0b-b98c-7bdcff465c2b"],
          "queryId": ["3694949039461716331"],
          "dbId": ["16384"]
        }
      },
      {
        name: "query_rows_avg",
        filters: {
          universeUuid: ["c623b9af-e863-4b0b-b98c-7bdcff465c2b"],
          "queryId": ["3694949039461716331"],
          "dbId": ["16384"]
        }
      },
      {
        name: "ysql_server_rpc_per_second",
        filters: {
          universeUuid: ["c623b9af-e863-4b0b-b98c-7bdcff465c2b"]
        }
      },
      {
        name: "cpu_usage",
        filters: {
          universeUuid: ["c623b9af-e863-4b0b-b98c-7bdcff465c2b"]
        }
      },
      {
        name: "disk_iops",
        filters: {
          universeUuid: ["c623b9af-e863-4b0b-b98c-7bdcff465c2b"]
        }
      },
      {
        name: "disk_bytes_per_second_per_node",
        filters: {
          universeUuid: ["c623b9af-e863-4b0b-b98c-7bdcff465c2b"]
        }
      },
      {
        name: "tserver_rpc_queue_size_tserver",
        filters: {
          universeUuid: ["c623b9af-e863-4b0b-b98c-7bdcff465c2b"]
        }
      },
      {
        name: "lsm_rocksdb_memory_rejections",
        filters: {
          universeUuid: ["c623b9af-e863-4b0b-b98c-7bdcff465c2b"]
        }
      },
      {
        name: "node_clock_skew",
        filters: {
          universeUuid: ["c623b9af-e863-4b0b-b98c-7bdcff465c2b"]
        }
      }
    ],
    defaultSettings: {
      splitMode: SplitMode.NONE,
      splitType: SplitType.NONE,
      splitCount: 0,
      returnAggregatedValue: false,
      aggregatedValueFunction: Aggregation.AVERAGE
    },
    rcaGuidelines: [
      {
        possibleCause: "Load increase",
        possibleCauseDescription: "RPS for this query or overall RPS increased significantly and DB is not able to process increased load",
        troubleshootingRecommendations: [
          "Check RPS graph for this query",
          "Check YSQL RPS graph"
        ]
      },
      {
        possibleCause: "Response size or queried tables sizes increased significantly",
        possibleCauseDescription: "DB have to process more data to process each request, hence latency grows",
        troubleshootingRecommendations: [
          "Check Average Rows graph for the query",
          "Check Table SST/WAL size graphs for tables, referenced in the query"
        ]
      },
      {
        possibleCause: "Query execution plan changed",
        possibleCauseDescription: "DB updated query execution plan based on the data statistics collected for requested tables",
        troubleshootingRecommendations: [
          "Check query execution plan via EXPLAIN ANALYSE"
        ]
      },
      {
        possibleCause: "DB internal queues contention",
        possibleCauseDescription: "RPC queues are growing and DB is not able to process all the requests on time. Typically all queries latency will grow.",
        troubleshootingRecommendations: [
          "Check RPC Queue Size graph"
        ]
      },
      {
        possibleCause: "Resource contention",
        possibleCauseDescription: "DB nodes face CPU, Memory or Disk IOPS/throughput limits. Typically all queries latency will grow.",
        troubleshootingRecommendations: [
          "Check CPU, Memory and Disk IOPS/throughput graphs"
        ]
      },
      {
        possibleCause: "Infra issues",
        possibleCauseDescription: "Network latency between DB nodes increased, Disk IOPS/throughput degraded, Network partitioning or other outage",
        troubleshootingRecommendations: [
          "Check network latency between the DB nodes",
          "Check all DB nodes are up and running",
          "Check Network graphs for anomaly"
        ]
      },
      {
        possibleCause: "Clock skew increased",
        possibleCauseDescription: "DB nodes clock became out of sync, which slows down queries processing",
        troubleshootingRecommendations: [
          "Check Clock Skew graph"
        ]
      }
    ]
  },
  {
    metadataUuid: "f9d72305-e793-4ea5-9195-5504bbe93048",
    uuid: "57d66488-d2ce-4134-881a-602725408a6c",
    category: AnomalyCategory.NODE,
    type: AnomalyType.HOT_NODE_CPU,
    universeUuid: "c623b9af-e863-4b0b-b98c-7bdcff465c2b",
    affectedNodes: [
      {
        name: "yb-15-troubleshooting-service-test-n2",
        uuid:"61b2e298-ae29-4170-a0da-135c67f83853"
      }
    ],
    affectedTables: [],
    title: "Uneven CPU usage distribution across DB nodes",
    summary: "Node 'yb-15-troubleshooting-service-test-n2' consume 55% more CPU than average of the other nodes.",
    detectionTime: "2024-01-19T13:07:18Z",
    startTime: "2024-01-19T10:10:00Z",
    graphStartTime: "2024-01-18T04:15:24Z",
    graphEndTime: "2024-01-19T13:07:18Z",
    mainGraphs: [
      {
        name: "cpu_usage",
        filters: {
          universeUuid: ["c623b9af-e863-4b0b-b98c-7bdcff465c2b"]
        }
      }
    ],
    supportingGraphs: [
      {
        name: "ysql_server_rpc_per_second",
        filters: {
          universeUuid: ["c623b9af-e863-4b0b-b98c-7bdcff465c2b"]
        }
      },
      {
        name: "tserver_rpcs_per_sec_by_universe",
        filters: {
          universeUuid: ["c623b9af-e863-4b0b-b98c-7bdcff465c2b"]
        }
      }
    ],
    defaultSettings: {
      splitMode: SplitMode.TOP,
      splitType: SplitType.NODE,
      splitCount: 3,
      returnAggregatedValue: true,
      aggregatedValueFunction: Aggregation.AVERAGE
    },
    rcaGuidelines: [
      {
        possibleCause: "Uneven data distribution",
        possibleCauseDescription: "Particular DB node or set of nodes have significantly more data then the other nodes",
        troubleshootingRecommendations: [
          "Check Disk Usage graph across DB nodes",
          "Check TServer read/write requests distribution across DB nodes"
        ]
      },
      {
        possibleCause: "Uneven query distribution",
        possibleCauseDescription: "Particular DB node or set of nodes process more SQL queries than the other nodes",
        troubleshootingRecommendations: [
          "Check YSQL RPC distribution across DB nodes"
        ]
      },
      {
        possibleCause: "3rd party processes",
        possibleCauseDescription: "Some process is running on the DB nodes which consumes CPU",
        troubleshootingRecommendations: [
          "Check top command output on the affected DB nodes"
        ]
      }
    ]
  }
];
