// Copyright (c) YugaByte, Inc.

import { FETCH_UNIVERSE_INFO, FETCH_UNIVERSE_INFO_SUCCESS, FETCH_UNIVERSE_INFO_FAILURE, RESET_UNIVERSE_INFO,
         CREATE_UNIVERSE, CREATE_UNIVERSE_SUCCESS, CREATE_UNIVERSE_FAILURE,
         EDIT_UNIVERSE, EDIT_UNIVERSE_SUCCESS, EDIT_UNIVERSE_FAILURE,
         FETCH_UNIVERSE_LIST, FETCH_UNIVERSE_LIST_SUCCESS, FETCH_UNIVERSE_LIST_FAILURE,
         RESET_UNIVERSE_LIST, DELETE_UNIVERSE, DELETE_UNIVERSE_SUCCESS,
         DELETE_UNIVERSE_FAILURE, FETCH_CUSTOMER_COST, FETCH_CUSTOMER_COST_SUCCESS,
         FETCH_CUSTOMER_COST_FAILURE, RESET_CUSTOMER_COST,
         FETCH_UNIVERSE_TASKS, FETCH_UNIVERSE_TASKS_SUCCESS,
         FETCH_UNIVERSE_TASKS_FAILURE, RESET_UNIVERSE_TASKS,
         OPEN_DIALOG, CLOSE_DIALOG, CONFIGURE_UNIVERSE_TEMPLATE, CONFIGURE_UNIVERSE_TEMPLATE_SUCCESS,
         CONFIGURE_UNIVERSE_TEMPLATE_FAILURE, CONFIGURE_UNIVERSE_RESOURCES, CONFIGURE_UNIVERSE_RESOURCES_SUCCESS,
         CONFIGURE_UNIVERSE_RESOURCES_FAILURE } from '../actions/universe';

const INITIAL_STATE = {currentUniverse: null, universeList: [], universeCurrentCostList: [],
                       currentTotalCost: 0, error: null, showModal: false, visibleModal: "",
                       formSubmitSuccess: false, universeConfigTemplate: {}, universeResourceTemplate: {}};

export default function(state = INITIAL_STATE, action) {
  let error;
  switch(action.type) {
    case CREATE_UNIVERSE:
      return { ...state, loading: true, formSubmitSuccess: false};
    case CREATE_UNIVERSE_SUCCESS:
      return { ...state, loading: false, universeConfigTemplate: {}, universeResourceTemplate: {}, formSubmitSuccess: true};
    case CREATE_UNIVERSE_FAILURE:
      error = action.payload.data || {message: action.payload.error};
      return { ...state, loading: false, error: error, formSubmitSuccess: false};
    case EDIT_UNIVERSE:
      return { ...state, loading: true, formSubmitSuccess: false};
    case EDIT_UNIVERSE_SUCCESS:
      return { ...state, loading: false, universeConfigTemplate: {}, universeResourceTemplate: {}, formSubmitSuccess: true};
    case EDIT_UNIVERSE_FAILURE:
      error = action.payload.data || {message: action.payload.error};
      return { ...state, loading: false, error: error, formSubmitSuccess: false};
    case OPEN_DIALOG:
      return { ...state, showModal: true, visibleModal: action.payload, formSubmitSuccess: false};
    case CLOSE_DIALOG:
      return { ...state, showModal: false, visibleModal: "", universeConfigTemplate: {}, universeResourceTemplate: {}};
    case FETCH_UNIVERSE_INFO:
      return { ...state, loading: true};
    case FETCH_UNIVERSE_INFO_SUCCESS:
      return { ...state, currentUniverse: action.payload.data, error: null, loading: false, showModal: false};
    case FETCH_UNIVERSE_INFO_FAILURE:
      error = action.payload.data || {message: action.payload.error};
      return { ...state, currentUniverse: null, error: error, loading: false};
    case RESET_UNIVERSE_INFO:
      return { ...state, currentUniverse: null, error: null, loading: false};
    case FETCH_UNIVERSE_LIST:
      return { ...state, universeList: [], error: null, loading: true};
    case FETCH_UNIVERSE_LIST_SUCCESS:
      return { ...state, universeList: action.payload.data, error: null, loading: false};
    case FETCH_UNIVERSE_LIST_FAILURE:
      return { ...state, universeList: [], error: error, loading: false};
    case RESET_UNIVERSE_LIST:
      return { ...state, universeList: [], universeCurrentCostList: [],
        currentTotalCost: 0, error: null, loading: false};
    case FETCH_UNIVERSE_TASKS:
      return { ...state, universeTasks: [], error: null, loading: true};
    case FETCH_UNIVERSE_TASKS_SUCCESS:
      return { ...state, universeTasks: action.payload.data, error: null, loading: false};
    case FETCH_UNIVERSE_TASKS_FAILURE:
      return { ...state, universeTasks: [], error: error, loading: false};
    case RESET_UNIVERSE_TASKS:
      return { ...state, universeTasks: [], error: null, loading: false};
    case DELETE_UNIVERSE:
      return { ...state, loading: true, error: null };
    case DELETE_UNIVERSE_SUCCESS:
      return { ...state, currentUniverse: null, error: null};
    case DELETE_UNIVERSE_FAILURE:
      return { ...state, error: action.payload.error}
    case FETCH_CUSTOMER_COST:
      return { ...state }
    case FETCH_CUSTOMER_COST_SUCCESS:
      var currentTotalCost = 0;
      for (var counter in action.payload) {
        if (action.payload.hasOwnProperty(counter)) {
          currentTotalCost += action.payload[counter].costPerMonth;
        }
      }
      return { ...state, universeCurrentCostList: action.payload,
               currentTotalCost: currentTotalCost}
    case FETCH_CUSTOMER_COST_FAILURE:
      return { ...state}
    case RESET_CUSTOMER_COST:
      return { ...state, currentTotalCost: 0, universeCurrentCostList: []}
    case CONFIGURE_UNIVERSE_TEMPLATE:
      return { ...state, universeConfigTemplate: {}, universeResourceTemplate: {}}
    case CONFIGURE_UNIVERSE_TEMPLATE_SUCCESS:
      return { ...state, universeConfigTemplate: action.payload.data}
    case CONFIGURE_UNIVERSE_TEMPLATE_FAILURE:
      return { ...state, universeConfigTemplate: {}, universeResourceTemplate: {}}
    case CONFIGURE_UNIVERSE_RESOURCES:
      return { ...state,  universeResourceTemplate: {}}
    case CONFIGURE_UNIVERSE_RESOURCES_SUCCESS:
      return { ...state, universeResourceTemplate: action.payload.data}
    case CONFIGURE_UNIVERSE_RESOURCES_FAILURE:
      return { ...state, universeResourceTemplate: {}}
    default:
      return state;
  }
}
