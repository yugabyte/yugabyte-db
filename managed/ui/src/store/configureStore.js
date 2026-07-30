// Copyright (c) YugabyteDB, Inc.

import configureStoreDev from './configureStore.dev.js';
import configureStoreProd from './configureStore.prod.js';

const configureStore =
  import.meta.env.PROD || (typeof window !== 'undefined' && window.location?.hostname !== 'localhost')
    ? configureStoreProd
    : configureStoreDev;

export default configureStore;
