// Copyright (c) YugabyteDB, Inc.

const configureStoreModule =
  import.meta.env.PROD || (window.location && window.location.hostname !== 'localhost')
    ? await import('./configureStore.prod.js')
    : await import('./configureStore.dev.js');

export default configureStoreModule.default;
