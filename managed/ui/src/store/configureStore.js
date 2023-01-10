// Copyright (c) YugaByte, Inc.

if (
  process.env.NODE_ENV === 'production' ||
  (window.location && window.location.hostname !== 'localhost')
) {
  // eslint-disable-next-line @typescript-eslint/no-require-imports
  module.exports = require('./configureStore.prod');
} else {
  // eslint-disable-next-line @typescript-eslint/no-require-imports
  module.exports = require('./configureStore.dev');
}
