// Copyright (c) YugaByte, Inc.

import _ from 'lodash';
import {
  VALIDATE_FROM_TOKEN,
  VALIDATE_FROM_TOKEN_RESPONSE,
  REGISTER,
  REGISTER_RESPONSE,
  FETCH_PASSWORD_POLICY,
  FETCH_PASSWORD_POLICY_RESPONSE,
  FETCH_ADMIN_NOTIFICATIONS,
  FETCH_ADMIN_NOTIFICATIONS_RESPONSE,
  LOGIN,
  LOGIN_RESPONSE,
  INVALID_CUSTOMER_TOKEN,
  RESET_TOKEN_ERROR,
  RESET_CUSTOMER,
  LOGOUT,
  LOGOUT_SUCCESS,
  LOGOUT_FAILURE,
  FETCH_SOFTWARE_VERSIONS_FAILURE,
  FETCH_SOFTWARE_VERSIONS_SUCCESS,
  FETCH_SOFTWARE_VERSIONS,
  FETCH_DB_VERSIONS,
  FETCH_DB_VERSIONS_SUCCESS,
  FETCH_DB_VERSIONS_FAILURE,
  FETCH_TLS_CERTS,
  FETCH_TLS_CERTS_RESPONSE,
  FETCH_OIDC_TOKEN,
  FETCH_OIDC_TOKEN_RESPONSE,
  ADD_TLS_CERT,
  ADD_TLS_CERT_RESPONSE,
  ADD_TLS_CERT_RESET,
  UPDATE_CERT,
  UPDATE_CERT_RESPONSE,
  FETCH_HOST_INFO,
  FETCH_HOST_INFO_SUCCESS,
  FETCH_HOST_INFO_FAILURE,
  FETCH_CUSTOMER_COUNT,
  FETCH_YUGAWARE_VERSION,
  FETCH_YUGAWARE_VERSION_RESPONSE,
  UPDATE_PROFILE,
  UPDATE_PROFILE_SUCCESS,
  UPDATE_PROFILE_FAILURE,
  ADD_CUSTOMER_CONFIG,
  ADD_CUSTOMER_CONFIG_RESPONSE,
  SET_INITIAL_VALUES,
  EDIT_CUSTOMER_CONFIG,
  EDIT_CUSTOMER_CONFIG_RESPONSE,
  FETCH_RUNTIME_CONFIGS,
  FETCH_RUNTIME_CONFIGS_RESPONSE,
  FETCH_RUNTIME_CONFIGS_KEY_INFO,
  FETCH_RUNTIME_CONFIGS_KEY_INFO_RESPONSE,
  FETCH_CUSTOMER_RUNTIME_CONFIGS,
  FETCH_CUSTOMER_RUNTIME_CONFIGS_RESPONSE,
  FETCH_PROVIDER_RUNTIME_CONFIGS,
  FETCH_PROVIDER_RUNTIME_CONFIGS_RESPONSE,
  SET_RUNTIME_CONFIG,
  SET_RUNTIME_CONFIG_RESPONSE,
  DELETE_RUNTIME_CONFIG,
  DELETE_RUNTIME_CONFIG_RESPONSE,
  RESET_RUNTIME_CONFIGS,
  FETCH_CUSTOMER_CONFIGS,
  FETCH_CUSTOMER_CONFIGS_RESPONSE,
  DELETE_CUSTOMER_CONFIG,
  DELETE_CUSTOMER_CONFIG_RESPONSE,
  GET_LOGS,
  GET_LOGS_SUCCESS,
  GET_LOGS_FAILURE,
  GET_RELEASES,
  GET_RELEASES_RESPONSE,
  REFRESH_RELEASES,
  REFRESH_RELEASES_RESPONSE,
  IMPORT_RELEASE,
  IMPORT_RELEASE_RESPONSE,
  UPDATE_RELEASE,
  UPDATE_RELEASE_RESPONSE,
  GET_ALERTS,
  GET_ALERTS_SUCCESS,
  GET_ALERTS_FAILURE,
  API_TOKEN_LOADING,
  API_TOKEN,
  API_TOKEN_RESPONSE,
  GET_SCHEDULES,
  GET_SCHEDULES_RESPONSE,
  DELETE_SCHEDULE,
  DELETE_SCHEDULE_RESPONSE,
  GET_CUSTOMER_USERS,
  GET_CUSTOMER_USERS_SUCCESS,
  GET_CUSTOMER_USERS_FAILURE,
  CREATE_USER,
  CREATE_USER_SUCCESS,
  CREATE_USER_FAILURE,
  CREATE_ALERT_CHANNEL,
  CREATE_ALERT_CHANNEL_RESPONSE,
  GET_ALERT_CHANNELS,
  GET_ALERT_DESTINATIONS,
  GET_ALERT_TEMPLATES,
  GET_ALERT_CONFIGS,
  CREATE_ALERT_DESTINATION,
  CREATE_ALERT_DESTINATION_RESPONSE,
  CREATE_ALERT_CONFIG,
  CREATE_ALERT_CONFIG_RESPONSE,
  UPDATE_ALERT_DESTINATION,
  UPDATE_ALERT_DESTINATION_RESPONSE,
  UPDATE_ALERT_CONFIG,
  UPDATE_ALERT_CONFIG_RESPONSE,
  DELETE_ALERT_DESTINATION,
  DELETE_ALERT_CONFIG,
  LOGS_FETCHING,
  FETCH_USER,
  FETCH_USER_SUCCESS,
  FETCH_USER_FAILURE,
  UPDATE_USER_PROFILE,
  UPDATE_USER_PROFILE_SUCCESS,
  UPDATE_USER_PROFILE_FAILURE
} from '../actions/customers';
import { compareYBSoftwareVersions, isVersionStable } from '../utils/universeUtilsTyped';

import { isDefinedNotNull } from '../utils/ObjectUtils';
import {
  getInitialState,
  setLoadingState,
  setSuccessState,
  setFailureState,
  setPromiseResponse
} from '../utils/PromiseUtils';

const INITIAL_STATE = {
  currentCustomer: getInitialState({}),
  currentUser: getInitialState({}),
  authToken: getInitialState({}),
  apiToken: getInitialState(null),
  adminNotifications: getInitialState({}),
  tasks: [],
  status: null,
  error: null,
  loading: false,
  softwareVersions: [],
  softwareVersionswithMetaData: [],
  dbVersionsWithMetadata: [],
  alerts: {
    alertsList: [],
    updated: null
  },
  alertChannels: getInitialState([]),
  alertDestinations: getInitialState([]),
  alertTemplates: getInitialState([]),
  alertConfigs: getInitialState([]),
  customers: getInitialState([]),
  deleteDestination: getInitialState([]),
  deleteAlertConfig: getInitialState([]),
  hostInfo: null,
  customerCount: {},
  OIDCToken: getInitialState({}),
  yugawareVersion: getInitialState({}),
  profile: getInitialState({}),
  addConfig: getInitialState({}),
  setInitialVal: getInitialState({}),
  editConfig: getInitialState({}),
  configs: getInitialState([]),
  deleteConfig: getInitialState({}),
  deleteSchedule: getInitialState({}),
  releases: getInitialState([]),
  refreshReleases: getInitialState({}),
  importRelease: getInitialState({}),
  updateRelease: getInitialState({}),
  addCertificate: getInitialState({}),
  userCertificates: getInitialState([]),
  users: getInitialState([]),
  schedules: getInitialState([]),
  createUser: getInitialState({}),
  createAlertChannel: getInitialState({}),
  createAlertDestination: getInitialState({}),
  createAlertConfig: getInitialState({}),
  updateAlertDestination: getInitialState({}),
  updateAlertConfig: getInitialState({}),
  providerRuntimeConfigs: getInitialState([]),
  customerRuntimeConfigs: getInitialState([])
};

export default function (state = INITIAL_STATE, action) {
  switch (action.type) {
    case VALIDATE_FROM_TOKEN:
      return setLoadingState(state, 'currentCustomer', {});
    case VALIDATE_FROM_TOKEN_RESPONSE:
      return setPromiseResponse(state, 'currentCustomer', action);
    case REGISTER:
      return setLoadingState(state, 'authToken', {});
    case REGISTER_RESPONSE:
      return setPromiseResponse(state, 'authToken', action);

    case FETCH_PASSWORD_POLICY:
      return { ...state, passwordValidationInfo: {} };
    case FETCH_PASSWORD_POLICY_RESPONSE:
      return { ...state, passwordValidationInfo: action.payload.data };

    case FETCH_ADMIN_NOTIFICATIONS:
      return setLoadingState(state, 'adminNotifications', {});
    case FETCH_ADMIN_NOTIFICATIONS_RESPONSE:
      return setPromiseResponse(state, 'adminNotifications', action);

    case LOGIN:
      return setLoadingState(state, 'authToken', {});
    case LOGIN_RESPONSE:
      return setPromiseResponse(state, 'authToken', action);

    case FETCH_USER:
      return setLoadingState(state, 'currentUser', {});
    case FETCH_USER_SUCCESS:
      return setPromiseResponse(state, 'currentUser', action.payload);
    case FETCH_USER_FAILURE:
      return setFailureState(state, 'currentUser', action.payload);

    case API_TOKEN_LOADING:
      return setLoadingState(state, 'apiToken', null);
    case API_TOKEN:
      return setLoadingState(state, 'apiToken', null);
    case API_TOKEN_RESPONSE:
      return setPromiseResponse(state, 'apiToken', action);
    case LOGOUT:
      return { ...state };
    case LOGOUT_SUCCESS:
      return { ...state, currentCustomer: getInitialState({}), authToken: getInitialState({}) };
    case LOGOUT_FAILURE:
      return { ...state };
    case INVALID_CUSTOMER_TOKEN:
      return { ...state, error: 'Invalid' };
    case RESET_TOKEN_ERROR:
      return { ...state, error: null };
    case RESET_CUSTOMER:
      return { ...state, currentCustomer: getInitialState({}), authToken: getInitialState({}) };
    // Remove - 2024.2
    case FETCH_SOFTWARE_VERSIONS:
      return { ...state, softwareVersions: [], softwareVersionswithMetaData: [] };
    // Remove - 2024.2
    case FETCH_SOFTWARE_VERSIONS_SUCCESS: {
      const sortedStableDbVersions = action.payload.data
        .filter((release) => isVersionStable(release))
        .sort((versionA, versionB) =>
          compareYBSoftwareVersions({
            versionA: versionB,
            versionB: versionA,
            options: {
              suppressFormatError: true,
              requireOrdering: true
            }
          })
        );
      const sortedPreviewDbVersions = action.payload.data
        .filter((release) => !isVersionStable(release))
        .sort((versionA, versionB) =>
          compareYBSoftwareVersions({
            versionA: versionB,
            versionB: versionA,
            options: {
              suppressFormatError: true,
              requireOrdering: true
            }
          })
        );
      const sortedVersions = sortedStableDbVersions.concat(sortedPreviewDbVersions);
      return {
        ...state,
        softwareVersions: sortedVersions,
        softwareVersionswithMetaData: action.payload.releasesWithMetadata
      };
    }
    // Remove 2024.2
    case FETCH_SOFTWARE_VERSIONS_FAILURE:
      return { ...state };

    case FETCH_DB_VERSIONS:
      return { ...state, softwareVersions: [], dbVersionsWithMetadata: [] };
    case FETCH_DB_VERSIONS_SUCCESS: {
      const sortedStableDbVersions = action.payload.data
        .filter((release) => isVersionStable(release))
        .sort((versionA, versionB) =>
          compareYBSoftwareVersions({
            versionA: versionB,
            versionB: versionA,
            options: {
              suppressFormatError: true,
              requireOrdering: true
            }
          })
        );

      const sortedPreviewDbVersions = action.payload.data
        .filter((release) => !isVersionStable(release))
        .sort((versionA, versionB) =>
          compareYBSoftwareVersions({
            versionA: versionB,
            versionB: versionA,
            options: {
              suppressFormatError: true,
              requireOrdering: true
            }
          })
        );
      const sortedVersions = sortedStableDbVersions.concat(sortedPreviewDbVersions);
      return {
        ...state,
        softwareVersions: sortedVersions,
        dbVersionsWithMetadata: action.payload.releasesWithMetadata
      };
    }
    case FETCH_DB_VERSIONS_FAILURE:
      return { ...state };

    case FETCH_TLS_CERTS:
      return setLoadingState(state, 'userCertificates', []);
    case FETCH_TLS_CERTS_RESPONSE:
      return setPromiseResponse(state, 'userCertificates', action);
    case ADD_TLS_CERT:
      return setLoadingState(state, 'addCertificate', {});
    case ADD_TLS_CERT_RESPONSE:
      if (action.payload.status !== 200) {
        if (isDefinedNotNull(action.payload.data)) {
          return setFailureState(state, 'addCertificate', action.payload.response.data.error);
        } else {
          return state;
        }
      }
      return setPromiseResponse(state, 'addCertificate', action);
    case ADD_TLS_CERT_RESET:
      return setLoadingState(state, 'addCertificate', getInitialState({}));
    case UPDATE_CERT:
      return setLoadingState(state, 'updateCert', {});
    case UPDATE_CERT_RESPONSE:
      return setLoadingState(state, 'updateCert', action);
    case FETCH_HOST_INFO:
      return { ...state, hostInfo: null };
    case FETCH_HOST_INFO_SUCCESS:
      return { ...state, hostInfo: action.payload.data };
    case FETCH_HOST_INFO_FAILURE:
      return { ...state, hostInfo: null };

    case FETCH_OIDC_TOKEN:
      return setLoadingState(state, 'OIDCToken', {});
    case FETCH_OIDC_TOKEN_RESPONSE:
      return setPromiseResponse(state, 'OIDCToken', action);
    case UPDATE_PROFILE:
      return setLoadingState(state, 'profile');
    case UPDATE_PROFILE_SUCCESS:
      return {
        ...setSuccessState(state, 'profile', 'updated-success'),
        currentCustomer: {
          ...state.currentCustomer,
          data: {
            ...state.currentCustomer.data,
            ...action.payload.data
          }
        }
      };
    case UPDATE_PROFILE_FAILURE:
      return setFailureState(state, 'profile', action.payload.response.data.error);

    case UPDATE_USER_PROFILE:
      return setLoadingState(state, 'profile');
    case UPDATE_USER_PROFILE_SUCCESS:
      return {
        ...setSuccessState(state, 'profile', 'updated-success'),
        currentUser: { ...action.payload }
      };
    case UPDATE_USER_PROFILE_FAILURE:
      return setFailureState(state, 'profile', action.payload.response.data.error);

    case FETCH_CUSTOMER_COUNT:
      return setLoadingState(state, 'customerCount');
    case GET_ALERTS:
      return {
        ...state,
        alerts: {
          alertsList: [],
          updated: null
        }
      };
    case GET_ALERTS_SUCCESS:
      return {
        ...state,
        alerts: {
          alertsList: action.payload.data,
          updated: Date.now()
        }
      };
    case GET_ALERTS_FAILURE:
      return {
        ...state,
        alerts: {
          alertsList: [],
          updated: Date.now()
        }
      };
    case GET_ALERT_CHANNELS:
      return setLoadingState(state, 'alertChannels', []);
    case GET_ALERT_DESTINATIONS:
      return setLoadingState(state, 'alertDestinations', []);
    case GET_ALERT_TEMPLATES:
      return setLoadingState(state, 'alertTemplates', []);
    case GET_ALERT_CONFIGS:
      return setLoadingState(state, 'alertConfigs', []);
    case DELETE_ALERT_DESTINATION:
      return setLoadingState(state, 'deleteDestination', []);
    case DELETE_ALERT_CONFIG:
      return setLoadingState(state, 'deleteAlertConfig', []);
    case CREATE_ALERT_CHANNEL:
      return setLoadingState(state, 'createAlertChannel', {});
    case CREATE_ALERT_CHANNEL_RESPONSE:
      if (action.payload.status !== 200) {
        if (isDefinedNotNull(action.payload.data)) {
          return setFailureState(state, 'createAlertChannel', action.payload.response.data.error);
        } else {
          return state;
        }
      }
      return setPromiseResponse(state, 'createAlertChannel', action);
    case CREATE_ALERT_DESTINATION:
      return setLoadingState(state, 'createAlertDestination', {});
    case CREATE_ALERT_DESTINATION_RESPONSE:
      if (action.payload.status !== 200) {
        if (isDefinedNotNull(action.payload.data)) {
          return setFailureState(state, 'createAlertChannel', action.payload.response.data.error);
        } else {
          return state;
        }
      }
      return setPromiseResponse(state, 'createAlertChannel', action);
    case CREATE_ALERT_CONFIG:
      return setLoadingState(state, 'createAlertConfig', {});
    case CREATE_ALERT_CONFIG_RESPONSE:
      if (action.payload.status !== 200) {
        if (isDefinedNotNull(action.payload.data)) {
          return setFailureState(state, 'createAlertChannel', action.payload.response.data.error);
        } else {
          return state;
        }
      }
      return setPromiseResponse(state, 'createAlertChannel', action);
    case UPDATE_ALERT_DESTINATION:
      return setLoadingState(state, 'updateAlertDestination', {});
    case UPDATE_ALERT_DESTINATION_RESPONSE:
      if (action.payload.status !== 200) {
        if (isDefinedNotNull(action.payload.data)) {
          return setFailureState(state, 'createAlertChannel', action.payload.response.data.error);
        } else {
          return state;
        }
      }
      return setPromiseResponse(state, 'createAlertChannel', action);
    case UPDATE_ALERT_CONFIG:
      return setLoadingState(state, 'updateAlertConfig', {});
    case UPDATE_ALERT_CONFIG_RESPONSE:
      if (action.payload.status !== 200) {
        if (isDefinedNotNull(action.payload.data)) {
          return setFailureState(state, 'createAlertChannel', action.payload.response.data.error);
        } else {
          return state;
        }
      }
      return setPromiseResponse(state, 'createAlertChannel', action);
    case FETCH_YUGAWARE_VERSION:
      return setLoadingState(state, 'yugawareVersion', {});
    case FETCH_YUGAWARE_VERSION_RESPONSE:
      return setPromiseResponse(state, 'yugawareVersion', action);
    case ADD_CUSTOMER_CONFIG:
      return setLoadingState(state, 'addConfig', {});
    case SET_INITIAL_VALUES:
      return {
        ...state,
        setInitialVal: action.payload
      };
    case ADD_CUSTOMER_CONFIG_RESPONSE:
      return setPromiseResponse(state, 'addConfig', action);
    case EDIT_CUSTOMER_CONFIG:
      return setLoadingState(state, 'editConfig', {});
    case EDIT_CUSTOMER_CONFIG_RESPONSE:
      return setPromiseResponse(state, 'editConfig', action);
    case FETCH_CUSTOMER_CONFIGS:
      return setLoadingState(state, 'configs', []);
    case FETCH_CUSTOMER_CONFIGS_RESPONSE:
      return setPromiseResponse(state, 'configs', action);
    case FETCH_RUNTIME_CONFIGS:
      return setLoadingState(state, 'runtimeConfigs', []);
    case FETCH_RUNTIME_CONFIGS_RESPONSE:
      return setPromiseResponse(state, 'runtimeConfigs', action);
    case FETCH_RUNTIME_CONFIGS_KEY_INFO:
      return setLoadingState(state, 'runtimeConfigsKeyMetadata', []);
    case FETCH_RUNTIME_CONFIGS_KEY_INFO_RESPONSE:
      return setPromiseResponse(state, 'runtimeConfigsKeyMetadata', action);
    case FETCH_PROVIDER_RUNTIME_CONFIGS:
      return setLoadingState(state, 'providerRuntimeConfigs', []);
    case FETCH_PROVIDER_RUNTIME_CONFIGS_RESPONSE:
      return setPromiseResponse(state, 'providerRuntimeConfigs', action);
    case FETCH_CUSTOMER_RUNTIME_CONFIGS:
      return setLoadingState(state, 'customerRuntimeConfigs', []);
    case FETCH_CUSTOMER_RUNTIME_CONFIGS_RESPONSE:
      return setPromiseResponse(state, 'customerRuntimeConfigs', action);
    case RESET_RUNTIME_CONFIGS:
      return setLoadingState(state, 'runtimeConfigs', []);
    case SET_RUNTIME_CONFIG:
      return setLoadingState(state, 'updateRuntimeConfig', {});
    case SET_RUNTIME_CONFIG_RESPONSE:
      return setPromiseResponse(state, 'updateRuntimeConfig', action);
    case DELETE_RUNTIME_CONFIG:
      return setLoadingState(state, 'deleteRuntimeConfig', {});
    case DELETE_RUNTIME_CONFIG_RESPONSE:
      return setPromiseResponse(state, 'deleteRuntimeConfig', action);
    case DELETE_CUSTOMER_CONFIG:
      return setLoadingState(state, 'deleteConfig', {});
    case DELETE_CUSTOMER_CONFIG_RESPONSE:
      return setPromiseResponse(state, 'deleteConfig', action);

    case LOGS_FETCHING:
    case GET_LOGS:
      return {
        ...state,
        yugaware_logs: null
      };
    case GET_LOGS_SUCCESS:
      return {
        ...state,
        yugaware_logs: action.payload.data,
        yugawareLogError: false
      };
    case GET_LOGS_FAILURE:
      return {
        ...state,
        yugaware_logs: null,
        yugawareLogError: true
      };

    case GET_CUSTOMER_USERS:
      return setLoadingState(state, 'users', getInitialState([]));
    case GET_CUSTOMER_USERS_SUCCESS:
      return setSuccessState(state, 'users', _.sortBy(action.payload.data, 'creationDate'));
    case GET_CUSTOMER_USERS_FAILURE:
      return setFailureState(state, 'users', action.payload);

    case CREATE_USER:
      return setLoadingState(state, 'createUser', {});
    case CREATE_USER_SUCCESS:
      return setSuccessState(state, 'createUser', action);
    case CREATE_USER_FAILURE:
      return setFailureState(state, 'createUser', action);

    case GET_RELEASES:
      return setLoadingState(state, 'releases', []);
    case GET_RELEASES_RESPONSE:
      return setPromiseResponse(state, 'releases', action);
    case REFRESH_RELEASES:
      return setLoadingState(state, 'refreshReleases', {});
    case REFRESH_RELEASES_RESPONSE:
      return setPromiseResponse(state, 'refreshReleases', action);
    case IMPORT_RELEASE:
      return setLoadingState(state, 'importRelease', {});
    case IMPORT_RELEASE_RESPONSE:
      return setPromiseResponse(state, 'importRelease', action);
    case UPDATE_RELEASE:
      return setLoadingState(state, 'updateRelease', {});
    case UPDATE_RELEASE_RESPONSE:
      return setPromiseResponse(state, 'updateRelease', action);
    case GET_SCHEDULES:
      return setLoadingState(state, 'schedules', []);
    case GET_SCHEDULES_RESPONSE:
      return setPromiseResponse(state, 'schedules', action);
    case DELETE_SCHEDULE:
      return setLoadingState(state, 'deleteSchedule', {});
    case DELETE_SCHEDULE_RESPONSE:
      return setPromiseResponse(state, 'deleteSchedule', action);

    default:
      return state;
  }
}
