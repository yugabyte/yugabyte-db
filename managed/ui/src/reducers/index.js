// Copyright (c) YugaByte, Inc.

import { combineReducers } from 'redux';
import CustomerReducer from './reducer_customer';
import CloudReducer from './reducer_cloud';
import UniverseReducer from './reducer_universe';
import ModalReducer from './reducer_modal';
import GraphReducer from './reducer_graph';
import TasksReducer from './reducer_tasks';
import TablesReducer from './reducer_tables';
import { FeatureFlag } from './feature';
import { reducer as formReducer } from 'redux-form';
import {SupportBundle} from "./support_bundle";
const rootReducer = combineReducers({
  customer: CustomerReducer,
  cloud: CloudReducer,
  universe: UniverseReducer,
  form: formReducer,
  modal: ModalReducer,
  graph: GraphReducer,
  tasks: TasksReducer,
  tables: TablesReducer,
  featureFlags: FeatureFlag,
  supportBundle: SupportBundle,
});

export default rootReducer;
