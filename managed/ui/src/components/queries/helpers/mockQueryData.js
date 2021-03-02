export const mockYcqlQueries = [
  {
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
    nodeName: 'test-node-driver-2',
    privateIp: '192.180.10.3',
    keyspace: 'potatomachine',
    query: 'SELECT * FROM cars WHERE brand = "Ford"',
    type: 'active',
    elapsedMillis: 10,
    clientHost: '10.19.120.7',
    clientPort: 9042
  },
];

export const mockYsqlQueries = [
  {
    nodeName: 'test-node-driver-1',
    privateIp: '192.180.10.1',
    dbName: 'postgres',
    query: 'SELECT * FROM cars WHERE price > 20000',
    type: 'active',
    elapsedMillis: 3,
    clientHost: '10.12.113.8',
    clientPort: 5433
  },
]

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