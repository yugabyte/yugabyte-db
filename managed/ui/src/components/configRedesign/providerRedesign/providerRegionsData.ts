// Sourced from opscli/ybops/data/aws-metadata.yml
export const AWS_REGIONS = {
  'ap-northeast-1': { zones: ['ap-northeast-1a', 'ap-northeast-1c', 'ap-northeast-1d'] },
  'ap-northeast-2': {
    zones: ['ap-northeast-2a', 'ap-northeast-2b', 'ap-northeast-2c', 'ap-northeast-2d']
  },
  'ap-northeast-3': { zones: ['ap-northeast-3a', 'ap-northeast-3b', 'ap-northeast-3c'] },
  'ap-south-1': { zones: ['ap-south-1a', 'ap-south-1b', 'ap-south-1c'] },
  'ap-southeast-1': { zones: ['ap-southeast-1a', 'ap-southeast-1b', 'ap-southeast-1c'] },
  'ap-southeast-2': { zones: ['ap-southeast-2a', 'ap-southeast-2b', 'ap-southeast-2c'] },
  'ap-southeast-3': { zones: ['ap-southeast-3a', 'ap-southeast-3b', 'ap-southeast-3c'] },
  'ca-central-1': { zones: ['ca-central-1a', 'ca-central-1b'] },
  'eu-central-1': { zones: ['eu-central-1a', 'eu-central-1b', 'eu-central-1c'] },
  'eu-west-1': { zones: ['eu-west-1a', 'eu-west-1b', 'eu-west-1c'] },
  'eu-west-2': { zones: ['eu-west-2a', 'eu-west-2b', 'eu-west-2c'] },
  'eu-west-3': { zones: ['eu-west-3a', 'eu-west-3b', 'eu-west-3c'] },
  'sa-east-1': { zones: ['sa-east-1a', 'sa-east-1b', 'sa-east-1c'] },
  'us-east-1': {
    zones: ['us-east-1a', 'us-east-1b', 'us-east-1c', 'us-east-1d', 'us-east-1e', 'us-east-1f']
  },
  'us-east-2': { zones: ['us-east-2a', 'us-east-2b', 'us-east-2c'] },
  'us-west-1': { zones: ['us-west-1a', 'us-west-1b'] },
  'us-west-2': { zones: ['us-west-2a', 'us-west-2b', 'us-west-2c'] },
  'us-gov-east-1': { zones: ['us-gov-east-1a', 'us-gov-east-1b', 'us-gov-east-1c'] },
  'us-gov-west-1': { zones: ['us-gov-west-1a', 'us-gov-west-1b', 'us-gov-west-1c'] }
} as const;

// Sourced from opscli/ybops/data/azu-metadata.yml
export const AZURE_REGIONS = {
  westus: { name: 'West US', zones: ['westus'] },
  westus2: { name: 'West US 2', zones: ['westus2-1', 'westus2-2', 'westus2-3'] },
  westus3: { name: 'West US 3', zones: ['westus3-1', 'westus3-2', 'westus3-3'] },
  westcentralus: { name: 'West Central US', zones: ['westcentralus'] },
  centralus: { name: 'Central US', zones: ['centralus-1', 'centralus-2', 'centralus-3'] },
  northcentralus: { name: 'North Central US', zones: ['northcentralus'] },
  southcentralus: {
    name: 'South Central US',
    zones: ['southcentralus-1', 'southcentralus-2', 'southcentralus-3']
  },
  eastus: { name: 'East US', zones: ['eastus-1', 'eastus-2', 'eastus-3'] },
  eastus2: { name: 'East US 2', zones: ['eastus2-1', 'eastus2-2', 'eastus2-3'] },
  canadacentral: {
    name: 'Canada Central',
    zones: ['canadacentral-1', 'canadacentral-2', 'canadacentral-3']
  },
  canadaeast: { name: 'Canada East', zones: ['canadaeast'] },
  westeurope: {
    name: 'West Europe',
    zones: ['westeurope-1', 'westeurope-2', 'westeurope-3']
  },
  northeurope: { name: 'North Europe', zones: ['northeurope-1', 'northeurope-2', 'northeurope-3'] },
  ukwest: { name: 'UK West', zones: ['ukwest'] },
  uksouth: { name: 'UK South', zones: ['uksouth-1', 'uksouth-2', 'uksouth-3'] },
  francecentral: {
    name: 'France Central',
    zones: ['francecentral-1', 'francecentral-2', 'francecentral-3']
  },
  germanywestcentral: {
    name: 'Germany West Central',
    zones: ['germanywestcentral-1', 'germanywestcentral-2', 'germanywestcentral-3']
  },
  norwayeast: { name: 'Norway East', zones: ['norwayeast-1', 'norwayeast-2', 'norwayeast-3'] },
  switzerlandnorth: {
    name: 'Switzerland North',
    zones: ['switzerlandnorth-1', 'switzerlandnorth-2', 'switzerlandnorth-3']
  },
  eastasia: { name: 'East Asia', zones: ['eastasia-1', 'eastasia-2', 'eastasia-3'] },
  southeastasia: {
    name: 'Southeast Asia',
    zones: ['southeastasia-1', 'southeastasia-2', 'southeastasia-3']
  },
  centralindia: {
    name: 'Central India',
    zones: ['centralindia-1', 'centralindia-2', 'centralindia-3']
  },
  southindia: { name: 'South India', zones: ['southindia'] },
  westindia: { name: 'West India', zones: ['westindia'] },
  japaneast: { name: 'Japan East', zones: ['japaneast-1', 'japaneast-2', 'japaneast-3'] },
  japanwest: { name: 'Japan West', zones: ['japanwest'] },
  koreacentral: {
    name: 'Korea Central',
    zones: ['koreacentral-1', 'koreacentral-2', 'koreacentral-3']
  },
  koreasouth: { name: 'Korea South', zones: ['koreasouth'] },
  uaenorth: { name: 'UAE North', zones: ['uaenorth-1', 'uaenorth-2', 'uaenorth-3'] },
  australiacentral: { name: 'Australia Central', zones: ['australiacentral'] },
  australiaeast: {
    name: 'Australia East',
    zones: ['australiaeast-1', 'australiaeast-2', 'australiaeast-3']
  },
  australiasoutheast: { name: 'Australia Southeast', zones: ['australiasoutheast'] },
  southafricanorth: {
    name: 'South Africa North',
    zones: ['southafricanorth-1', 'southafricanorth-2', 'southafricanorth-3']
  },
  brazilsouth: { name: 'Brazil South', zones: ['brazilsouth-1', 'brazilsouth-2', 'brazilsouth-3'] }
} as const;

// Sourced from opscli/ybops/data/gcp-metadata.yml
export const GCP_REGIONS: Record<string, { zones: readonly string[] }> = {
  'asia-east1': { zones: ['a', 'b', 'c'] },
  'asia-east2': { zones: ['a', 'b', 'c'] },
  'asia-northeast1': { zones: ['a', 'b', 'c'] },
  'asia-northeast2': { zones: ['a', 'b', 'c'] },
  'asia-northeast3': { zones: ['a', 'b', 'c'] },
  'asia-south1': { zones: ['a', 'b', 'c'] },
  'asia-south2': { zones: ['a', 'b', 'c'] },
  'asia-southeast1': { zones: ['a', 'b', 'c'] },
  'asia-southeast2': { zones: ['a', 'b', 'c'] },
  'australia-southeast1': { zones: ['a', 'b', 'c'] },
  'australia-southeast2': { zones: ['a', 'b', 'c'] },
  'europe-central2': { zones: ['a', 'b', 'c'] },
  'europe-north1': { zones: ['a', 'b', 'c'] },
  'europe-west1': { zones: ['b', 'c', 'd'] },
  'europe-west2': { zones: ['a', 'b', 'c'] },
  'europe-west3': { zones: ['a', 'b', 'c'] },
  'europe-west4': { zones: ['a', 'b', 'c'] },
  'europe-west6': { zones: ['a', 'b', 'c'] },
  'northamerica-northeast1': { zones: ['a', 'b', 'c'] },
  'northamerica-northeast2': { zones: ['a', 'b', 'c'] },
  'southamerica-east1': { zones: ['a', 'b', 'c'] },
  'us-central1': { zones: ['a', 'b', 'c', 'f'] },
  'us-east1': { zones: ['b', 'c', 'd'] },
  'us-east4': { zones: ['a', 'b', 'c'] },
  'us-west1': { zones: ['a', 'b', 'c'] },
  'us-west2': { zones: ['a', 'b', 'c'] },
  'us-west3': { zones: ['a', 'b', 'c'] },
  'us-west4': { zones: ['a', 'b', 'c'] }
} as const;

export const KUBERNETES_REGIONS = {
  'us-west-1': { name: 'US West (N. California)', latitude: 37, longitude: -121 },
  'us-west-2': { name: 'US West (Oregon)', latitude: 44, longitude: -121 },
  'us-east-1': { name: 'US East (N. Virginia)', latitude: 36.8, longitude: -79 },
  'us-east-2': { name: 'US East (Ohio)', latitude: 40, longitude: -83 },
  'us-south': { name: 'US South', latitude: 28, longitude: -99 },
  'us-north': { name: 'US North', latitude: 48, longitude: -118 },
  'south-asia': { name: 'South Asia', latitude: 18.4, longitude: 78.4 },
  'south-east-asia': { name: 'SE Asia', latitude: 14, longitude: 101 },
  'new-zealand': { name: 'New Zealand', latitude: -43, longitude: 171 },
  japan: { name: 'Japan', latitude: 36, longitude: 139 },
  'eu-west-1': { name: 'Europe (Ireland)', latitude: 53, longitude: -9 },
  'eu-west-2': { name: 'Europe (London)', latitude: 51, longitude: 0 },
  'eu-west-3': { name: 'Europe (Paris)', latitude: 48, longitude: 3 },
  'eu-west-4': { name: 'Europe (Amsterdam)', latitude: 52, longitude: 5 },
  'eu-central-1': { name: 'Europe (Frankfurt)', latitude: 50, longitude: 9 },
  'ap-southeast-1': { name: 'Asia Pacific (Singapore)', latitude: 5, longitude: 104 },
  'ap-southeast-2': { name: 'Asia Pacific (Sydney)', latitude: -34, longitude: 151 },
  'ap-northeast-1': { name: 'Asia Pacific (Tokyo)', latitude: 35.7, longitude: 140 },
  'ap-northeast-2': { name: 'Asia Pacific (Seoul)', latitude: 37.5, longitude: 127 },
  'ap-northeast-3': { name: 'Asia Pacific (Osaka)', latitude: 34.7, longitude: 135.5 },
  'sa-east-1': { name: 'South America (Sao Paulo)', latitude: -23.5, longitude: -46.7 },
  'ap-south-1': { name: 'Asia Pacific (Mumbai)', latitude: 19, longitude: 72.4 },
  'ca-central-1': { name: 'Montreal (Canada)', latitude: 45.5, longitude: -73.4 },
  'me-south-1': { name: 'Middle East (Bahrain)', latitude: 26, longitude: 50.5 },
  'ap-east-1': { name: 'Asia Pacific (Hong Kong)', latitude: 22.3, longitude: 114 },
  'af-south-1': { name: 'Africa (Cape Town)', latitude: -33.9, longitude: 18.4 },
  'eu-south-1': { name: 'Europe (Milan)', latitude: 45.4, longitude: 9.1 },
  'eu-north-1': { name: 'Europe (Stockholm)', latitude: 59.3, longitude: 18 },
  'eu-east': { name: 'EU East', latitude: 46, longitude: 25 },
  china: { name: 'China', latitude: 31.2, longitude: 121.5 },
  brazil: { name: 'Brazil', latitude: -22, longitude: -43 },
  australia: { name: 'Australia', latitude: -29, longitude: 148 }
} as const;

export const ON_PREM_UNLISTED_LOCATION = 'Unlisted';
export const ON_PREM_LOCATIONS = {
  Australia: { latitude: -29, longitude: 148 },
  Brazil: { latitude: -22, longitude: -43 },
  China: { latitude: 31.2, longitude: 121.5 },
  'EU East': { latitude: 46, longitude: 25 },
  'EU West': { latitude: 48, longitude: 3 },
  Japan: { latitude: 36, longitude: 139 },
  'New Zealand': { latitude: -43, longitude: 171 },
  'SE Asia': { latitude: 14, longitude: 101 },
  'South Asia': { latitude: 18.4, longitude: 78.4 },
  'US East': { latitude: 36.8, longitude: -79 },
  'US North': { latitude: 48, longitude: -118 },
  'US South': { latitude: 28, longitude: -99 },
  'US West': { latitude: 37, longitude: -121 },
  'EU West - UK': { latitude: 55, longitude: -3 },
  'EU East - Istanbul': { latitude: 41, longitude: 29 },
  'EU Central - Frankfurt': { latitude: 50.1, longitude: 8.7 },
  'EU North - Stockholm': { latitude: 59.3, longitude: 18 },
  'EU South - Milan': { latitude: 45.5, longitude: 9.2 },
  'Middle East - Bahrain': { latitude: 26, longitude: 50.5 },
  'South Africa - Johannesburg': { latitude: -26.2, longitude: 28.04 },
  [ON_PREM_UNLISTED_LOCATION]: { latitude: 0, longitude: 0 }
} as const;
