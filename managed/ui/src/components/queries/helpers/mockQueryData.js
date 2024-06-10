export const mockYcqlQueries = [
  {
    id: 'fake-id-ycql-001',
    nodeName: 'test-node-driver-1',
    privateIp: '192.180.10.1',
    keyspace: 'potatomachine',
    query: 'SELECT * FROM cars WHERE age < 10',
    type: 'active',
    elapsedMillis: 3,
    clientHost: '10.19.120.7',
    clientPort: 9042
  },
  {
    id: 'fake-id-ycql-002',
    nodeName: 'test-node-republic-2',
    privateIp: '192.180.10.2',
    keyspace: 'potatomachine',
    query: 'INSERT INTO recipe VALUES (101, "Easiest Potato Oven Recipes", 5, 8)',
    type: 'active',
    elapsedMillis: 15,
    clientHost: '10.19.120.7',
    clientPort: 9042
  },
  {
    id: 'fake-id-ycql-003',
    nodeName: 'test-node-driver-2',
    privateIp: '192.180.10.3',
    keyspace: 'potatomachine',
    query: 'SELECT * FROM cars WHERE brand = "Ford"',
    type: 'active',
    elapsedMillis: 10,
    clientHost: '10.19.120.7',
    clientPort: 9042
  }
];

export const mockLiveYsqlQueries = [
  {
    id: 'fake-id-ysql-001',
    nodeName: 'test-node-driver-1',
    privateIp: '192.180.10.1',
    dbName: 'postgres',
    query: 'SELECT * FROM cars WHERE price > 20000',
    sessionStatus: 'active',
    elapsedMillis: 3,
    clientHost: '10.12.113.8',
    clientPort: 5433
  }
];

export const mockSlowYsqlQueries = [
  {
    calls: 5,
    datname: 'postgres',
    local_blks_hit: 0,
    local_blks_written: 0,
    max_time: 0.85729,
    mean_time: 0.4596,
    min_time: 0.12058,
    query: 'INSERT INTO test_table VALUES ($1, $2)',
    queryid: 3155616829219000,
    rolname: 'yugabyte',
    rows: 5,
    stddev_time: 0.00252,
    total_time: 3.05239
  },
  {
    calls: 227,
    datname: 'postgres',
    local_blks_hit: 0,
    local_blks_written: 0,
    max_time: 0.041366,
    mean_time: 0.024098,
    min_time: 0.020445,
    query: 'SET extra_float_digits = 3',
    queryid: 5125610066150999000,
    rolname: 'yugabyte',
    rows: 0,
    stddev_time: 0.001914,
    total_time: 5.4704
  }
];

export const mockKeyMap = {
  'Node Name': {
    value: 'nodeName',
    type: 'string'
  },
  'Private IP': {
    value: 'privateIp',
    type: 'string'
  },
  Keyspace: {
    value: 'keyspace',
    type: 'string'
  },
  'DB Name': {
    value: 'dbName',
    type: 'string'
  },
  Query: {
    value: 'query',
    type: 'string'
  },
  'Elapsed Time': {
    value: 'elapsedMillis',
    type: 'number'
  },
  Type: {
    value: 'type',
    type: 'string'
  },
  'Client Host': {
    value: 'clientHost',
    type: 'string'
  },
  'Client Port': {
    value: 'clientPort',
    type: 'string'
  },
  'Session Status': {
    value: 'sessionStatus',
    type: 'string'
  },
  'Client Name': {
    value: 'appName',
    type: 'string'
  }
};

export const mockSearchTokens = [
  {
    key: 'nodeName',
    label: 'Node Name',
    value: 'driver'
  }
];
