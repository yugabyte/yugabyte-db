// Copyright (c) YugaByte, Inc.

import { FETCH_UNIVERSE_INFO, FETCH_UNIVERSE_INFO_SUCCESS, FETCH_UNIVERSE_INFO_FAILURE, RESET_UNIVERSE_INFO,
  CREATE_UNIVERSE, CREATE_UNIVERSE_SUCCESS, CREATE_UNIVERSE_FAILURE,
  EDIT_UNIVERSE, EDIT_UNIVERSE_SUCCESS, EDIT_UNIVERSE_FAILURE,
  FETCH_UNIVERSE_LIST, FETCH_UNIVERSE_LIST_SUCCESS, FETCH_UNIVERSE_LIST_FAILURE,
  RESET_UNIVERSE_LIST, DELETE_UNIVERSE, DELETE_UNIVERSE_SUCCESS,
  DELETE_UNIVERSE_FAILURE, FETCH_UNIVERSE_TASKS, FETCH_UNIVERSE_TASKS_SUCCESS,
  FETCH_UNIVERSE_TASKS_FAILURE, RESET_UNIVERSE_TASKS,
  OPEN_DIALOG, CLOSE_DIALOG, CONFIGURE_UNIVERSE_TEMPLATE, CONFIGURE_UNIVERSE_TEMPLATE_SUCCESS,
  CONFIGURE_UNIVERSE_TEMPLATE_FAILURE, CONFIGURE_UNIVERSE_RESOURCES, CONFIGURE_UNIVERSE_RESOURCES_SUCCESS,
  CONFIGURE_UNIVERSE_RESOURCES_FAILURE, ROLLING_UPGRADE, ROLLING_UPGRADE_SUCCESS, ROLLING_UPGRADE_FAILURE,
  RESET_ROLLING_UPGRADE, SET_UNIVERSE_METRICS, SET_PLACEMENT_STATUS, RESET_UNIVERSE_CONFIGURATION }
  from '../actions/universe';
import _ from 'lodash';
import { isNonEmptyArray } from 'utils/ObjectUtils.js';

const INITIAL_STATE = {currentUniverse: null, universeList: [], error: null, showModal: false, visibleModal: "",
  formSubmitSuccess: false, universeConfigTemplate: {}, universeResourceTemplate: {},
  currentPlacementStatus: null, loading: {createUniverse: false, editUniverse: false, currentUniverse: false,
    universeList: false, universeTasks: false}};

export default function(state = INITIAL_STATE, action) {
  let error;
  switch(action.type) {
    case CREATE_UNIVERSE:
      return { ...state, loading: _.assign(state.loading, {createUniverse: true}), formSubmitSuccess: false};
    case CREATE_UNIVERSE_SUCCESS:
      return { ...state, loading: _.assign(state.loading, {createUniverse: false}), universeConfigTemplate: {}, universeResourceTemplate: {}, formSubmitSuccess: true};
    case CREATE_UNIVERSE_FAILURE:
      return { ...state, loading: _.assign(state.loading, {createUniverse: false}), error: action.payload.data.error, formSubmitSuccess: false};
    case EDIT_UNIVERSE:
      return { ...state, loading: _.assign(state.loading, {editUniverse: true}), formSubmitSuccess: false};
    case EDIT_UNIVERSE_SUCCESS:
      return { ...state, loading: _.assign(state.loading, {editUniverse: false}), universeConfigTemplate: {}, universeResourceTemplate: {}, formSubmitSuccess: true};
    case EDIT_UNIVERSE_FAILURE:
      return { ...state, loading: _.assign(state.loading, {editUniverse: false}), error: action.payload.data.error, formSubmitSuccess: false};
    case OPEN_DIALOG:
      return { ...state, showModal: true, visibleModal: action.payload, formSubmitSuccess: false};
    case CLOSE_DIALOG:
      return { ...state, showModal: false, visibleModal: "", universeConfigTemplate: {}, universeResourceTemplate: {}};
    case FETCH_UNIVERSE_INFO:
      return { ...state, loading: _.assign(state.loading, {currentUniverse: true})};
    case FETCH_UNIVERSE_INFO_SUCCESS:
      return { ...state, currentUniverse: action.payload.data, error: null, loading: _.assign(state.loading, {currentUniverse: false}), showModal: false};
    case FETCH_UNIVERSE_INFO_FAILURE:
      error = action.payload.data || {message: action.payload.error};
      return { ...state, currentUniverse: null, error: error, loading: _.assign(state.loading, {currentUniverse: false})};
    case RESET_UNIVERSE_INFO:
      return { ...state, currentUniverse: null, error: null, loading: _.assign(state.loading, {currentUniverse: false})};
    case FETCH_UNIVERSE_LIST:
      return { ...state, universeList: [], error: null, loading: _.assign(state.loading, {universeList: true})};
    case FETCH_UNIVERSE_LIST_SUCCESS:
      return { ...state, universeList: action.payload.data, error: null, loading: _.assign(state.loading, {universeList: false})};
    case FETCH_UNIVERSE_LIST_FAILURE:
      return { ...state, universeList: [], error: error, loading: _.assign(state.loading, {universeList: false})};
    case RESET_UNIVERSE_LIST:
      return { ...state, universeList: [], universeCurrentCostList: [],
        currentTotalCost: 0, error: null, loading: _.assign(state.loading, {universeList: false})};
    case FETCH_UNIVERSE_TASKS:
      return { ...state, universeTasks: [], error: null, loading: _.assign(state.loading, {universeTasks: true})};
    case FETCH_UNIVERSE_TASKS_SUCCESS:
      return { ...state, universeTasks: action.payload.data, error: null, loading: _.assign(state.loading, {universeTasks: false})};
    case FETCH_UNIVERSE_TASKS_FAILURE:
      return { ...state, universeTasks: [], error: error, loading: _.assign(state.loading, {universeTasks: false})};
    case RESET_UNIVERSE_TASKS:
      return { ...state, universeTasks: [], error: null, loading: false};
    case DELETE_UNIVERSE:
      return { ...state, loading: true, error: null };
    case DELETE_UNIVERSE_SUCCESS:
      return { ...state, currentUniverse: null, error: null};
    case DELETE_UNIVERSE_FAILURE:
      return { ...state, error: action.payload.error}
    case CONFIGURE_UNIVERSE_TEMPLATE:
      return { ...state, universeConfigTemplate: {}, universeResourceTemplate: {}}
    case CONFIGURE_UNIVERSE_TEMPLATE_SUCCESS:
      return { ...state, universeConfigTemplate: action.payload.data, error: null}
    case CONFIGURE_UNIVERSE_TEMPLATE_FAILURE:
      return { ...state, universeConfigTemplate: {}, universeResourceTemplate: {}, error: action.payload.data.error}
    case CONFIGURE_UNIVERSE_RESOURCES:
      return { ...state,  universeResourceTemplate: {}}
    case CONFIGURE_UNIVERSE_RESOURCES_SUCCESS:
      return { ...state, universeResourceTemplate: action.payload.data}
    case CONFIGURE_UNIVERSE_RESOURCES_FAILURE:
      return { ...state, universeResourceTemplate: {}}
    case ROLLING_UPGRADE:
      return { ...state, error: null};
    case ROLLING_UPGRADE_SUCCESS:
      return { ...state, error: null, formSubmitSuccess: true};
    case ROLLING_UPGRADE_FAILURE:
      return {...state, error: action.payload.data.error, formSubmitSuccess: false};
    case RESET_ROLLING_UPGRADE:
      return { ...state, error: null};
    case SET_UNIVERSE_METRICS:
      var currentUniverseList = state.universeList;
      var universeReadWriteMetricList = action.payload.data.disk_iops_by_universe.data;
      if (isNonEmptyArray(universeReadWriteMetricList)) {
        universeReadWriteMetricList.forEach(function(metricData, metricIdx) {
          for (var counter = 0; counter < currentUniverseList.length; counter++) {
            if (currentUniverseList[counter].universeDetails.nodePrefix === metricData.name) {
              currentUniverseList[counter][metricData.labels["type"]] = metricData;
            }
          }
        });
      }
      return {...state, universeList: currentUniverseList}
    case SET_PLACEMENT_STATUS:
      return {...state, currentPlacementStatus: action.payload}
    case RESET_UNIVERSE_CONFIGURATION:
      return {...state, currentPlacementStatus: null, universeResourceTemplate: {}, universeConfigTemplate: {}}
    default:
      return state;
  }
}
