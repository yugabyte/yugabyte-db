// Copyright (c) YugaByte, Inc.
import _ from 'lodash';
import Cookies from 'js-cookie';

export const IN_DEVELOPMENT_MODE = process.env.NODE_ENV === 'development';

// NOTE: when using REACT_APP_YUGAWARE_API_URL at local development - after login with SSO it will
// set auth cookies for API host domain and redirect to API host root instead of localhost:3000/
// Need to manually set "userId", "customerId" and "PLAY_SESSION" cookies for localhost:3000
export const ROOT_URL =
  process.env.REACT_APP_YUGAWARE_API_URL ??
  (IN_DEVELOPMENT_MODE ? 'http://localhost:9000/api/v1' : '/api/v1');

// Allow requests made to endpoints in ‘routes’ file.
export const BASE_URL = IN_DEVELOPMENT_MODE ? 'http://localhost:9000' : '';

export const REACT_TROUBLESHOOT_API_DEV_URL =
  process.env.REACT_TROUBLESHOOT_API_DEV_URL ?? `http://localhost:8080`;
export const REACT_TROUBLESHOOT_API_PROD_URL =
  process.env.REACT_TROUBLESHOOT_API_PROD_URL ?? `https://10.9.15.156:8443/api`;

export const MAP_SERVER_URL = IN_DEVELOPMENT_MODE
  ? `https://s3-us-west-2.amazonaws.com/${process.env.REACT_APP_YB_MAP_URL}/map`
  : '/static/map';

// get SSO flag from global config loaded in index.html before UI app started
export const USE_SSO = _.get(window, 'YB_Platform_Config.use_oauth', false);

export const isSSOLogin = () => Cookies.get('apiToken') ?? localStorage.getItem('apiToken');

export const isSSOEnabled = () => _.get(window, 'YB_Platform_Config.use_oauth', false);

export const setSSO = (value) => _.set(window, 'YB_Platform_Config.use_oauth', value);

export const setShowJWTTokenInfo = (value) =>
  _.set(window, 'YB_Platform_Config.show_jwt_token_info', value);

export const shouldShowJWTTokenInfo = () =>
  _.get(window, 'YB_Platform_Config.show_jwt_token_info', false);

// TODO : probably fetch the provider metadata (name and code from backend)
export const PROVIDER_TYPES = [
  { code: 'aws', name: 'Amazon', label: 'Amazon Web Services' },
  { code: 'docker', name: 'Docker Localhost', label: 'Docker' },
  { code: 'azu', name: 'Azure', label: 'Microsoft Azure' },
  { code: 'gcp', name: 'Google', label: 'Google Cloud' },
  { code: 'onprem', name: 'On Premises', label: 'On-Premises Datacenter' },
  { code: 'kubernetes', name: 'Kubernetes', label: 'Kubernetes' },
  { code: 'cloud-1', name: 'Cloud-1', label: 'Cloud-1' },
  { code: 'other', name: 'Other', label: 'Custom Datacenter' }
];

// The following region array is used in the old kubernetes provider form.
// Please also add any new K8s regions and zones to:
// src/components/configRedesign/providerRedesign/providerRegionsData.ts
// so that the new kubernetes provider UI stays in sync.
// The old provider UI and its related components/constants/types will be removed once
// it is no longer used as a fallback option.
export const REGION_METADATA = [
  { code: 'us-west-1', name: 'US West (N. California)', latitude: 37, longitude: -121 },
  { code: 'us-west-2', name: 'US West (Oregon)', latitude: 44, longitude: -121 },
  { code: 'us-east-1', name: 'US East (N. Virginia)', latitude: 36.8, longitude: -79 },
  { code: 'us-east-2', name: 'US East (Ohio)', latitude: 40, longitude: -83 },
  { code: 'us-south', name: 'US South', latitude: 28, longitude: -99 },
  { code: 'us-north', name: 'US North', latitude: 48, longitude: -118 },
  { code: 'south-asia', name: 'South Asia', latitude: 18.4, longitude: 78.4 },
  { code: 'south-east-asia', name: 'SE Asia', latitude: 14, longitude: 101 },
  { code: 'new-zealand', name: 'New Zealand', latitude: -43, longitude: 171 },
  { code: 'japan', name: 'Japan', latitude: 36, longitude: 139 },
  { code: 'eu-west-1', name: 'Europe (Ireland)', latitude: 53, longitude: -9 },
  { code: 'eu-west-2', name: 'Europe (London)', latitude: 51, longitude: 0 },
  { code: 'eu-west-3', name: 'Europe (Paris)', latitude: 48, longitude: 3 },
  { code: 'eu-west-4', name: 'Europe (Amsterdam)', latitude: 52, longitude: 5 },
  { code: 'eu-central-1', name: 'Europe (Frankfurt)', latitude: 50, longitude: 9 },
  { code: 'ap-southeast-1', name: 'Asia Pacific (Singapore)', latitude: 5, longitude: 104 },
  { code: 'ap-southeast-2', name: 'Asia Pacific (Sydney)', latitude: -34, longitude: 151 },
  { code: 'ap-northeast-1', name: 'Asia Pacific (Tokyo)', latitude: 35.7, longitude: 140 },
  { code: 'ap-northeast-2', name: 'Asia Pacific (Seoul)', latitude: 37.5, longitude: 127 },
  { code: 'ap-northeast-3', name: 'Asia Pacific (Osaka)', latitude: 34.7, longitude: 135.5 },
  { code: 'sa-east-1', name: 'South America (Sao Paulo)', latitude: -23.5, longitude: -46.7 },
  { code: 'ap-south-1', name: 'Asia Pacific (Mumbai)', latitude: 19, longitude: 72.4 },
  { code: 'ca-central-1', name: 'Montreal (Canada)', latitude: 45.5, longitude: -73.4 },
  { code: 'me-south-1', name: 'Middle East (Bahrain)', latitude: 26, longitude: 50.5 },
  { code: 'ap-east-1', name: 'Asia Pacific (Hong Kong)', latitude: 22.3, longitude: 114 },
  { code: 'af-south-1', name: 'Africa (Cape Town)', latitude: -33.9, longitude: 18.4 },
  { code: 'eu-south-1', name: 'Europe (Milan)', latitude: 45.4, longitude: 9.1 },
  { code: 'eu-north-1', name: 'Europe (Stockholm)', latitude: 59.3, longitude: 18 },
  { code: 'eu-east', name: 'EU East', latitude: 46, longitude: 25 },
  { code: 'china', name: 'China', latitude: 31.2, longitude: 121.5 },
  { code: 'brazil', name: 'Brazil', latitude: -22, longitude: -43 },
  { code: 'australia', name: 'Australia', latitude: -29, longitude: 148 }
];

export const REGION_DICT = {};
REGION_METADATA.forEach((region, index) => {
  REGION_DICT[region.code] = {
    ...region,
    index // Track position in array for ease of access
  };
});

export const KUBERNETES_PROVIDERS = [
  // keep deprecated PKS to properly show existing PKS providers in the list, if any
  { code: 'pks', name: 'Pivotal Container Service' },
  { code: 'tanzu', name: 'VMware Tanzu' },
  { code: 'openshift', name: 'Red Hat OpenShift' },
  { code: 'gke', name: 'Google Kubernetes Engine' },
  { code: 'aks', name: 'Azure Kubernetes Service' },
  { code: 'eks', name: 'Elastic Kubernetes Service' },
  { code: 'custom', name: 'Custom Kubernetes Service' }
];

//TODO : Move this to translation json
export const YUGABYTE_TITLE = 'YugabyteDB Anywhere';
