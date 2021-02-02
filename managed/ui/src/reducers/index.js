// Copyright (c) YugaByte, Inc.

import { combineReducers } from 'redux';
import CustomerReducer from './reducer_customer';
import CloudReducer from './reducer_cloud';
import UniverseReducer from './reducer_universe';
import ModalReducer from './reducer_modal';
import GraphReducer from './reducer_graph';
import TasksReducer from './reducer_tasks';
import TablesReducer from './reducer_tables';
import ToastReducer from './reducer_toaster';
import { reducer as formReducer } from 'redux-form';

const rootReducer = combineReducers({
  customer: CustomerReducer,
  cloud: CloudReducer,
  universe: UniverseReducer,
  form: formReducer,
  modal: ModalReducer,
  graph: GraphReducer,
  tasks: TasksReducer,
  tables: TablesReducer,
  toast: ToastReducer
});

export default rootReducer;
