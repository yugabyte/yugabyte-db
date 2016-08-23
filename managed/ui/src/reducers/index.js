// Copyright (c) YugaByte, Inc.

import { combineReducers } from 'redux';
import CustomerReducer from './reducer_customer';
import UniverseReducer from './reducer_universe';
import { reducer as formReducer } from 'redux-form';

const rootReducer = combineReducers({
  customer: CustomerReducer,
  form: formReducer,
  universe: UniverseReducer
});

export default rootReducer;
