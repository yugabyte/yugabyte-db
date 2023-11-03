// The following region arrays are used in the old provider form.
// Please also add any new regions and zones to:
// src/components/configRedesign/providerRedesign/providerRegionsData.ts
// so that the new kubernetes provider UI stays in sync.
// The old provider UI and its related components/constants/types will be removed once
// it is no longer used as a fallback option.

// These should match the metadata in devops under opscli/ybops/data/aws-metadata.yml
export const AWS_REGIONS = [
  {
    destVpcRegion: 'ap-northeast-1',
    zones: ['ap-northeast-1a', 'ap-northeast-1c', 'ap-northeast-1d']
  },
  // This is disabled in devops, so keep it disabled here as well. Pending on fetching data from a
  // YW API: ENG-4225
  {
    destVpcRegion: 'ap-northeast-2',
    zones: ['ap-northeast-2a', 'ap-northeast-2b', 'ap-northeast-2c', 'ap-northeast-2d']
  },
  {
    destVpcRegion: 'ap-northeast-3',
    zones: ['ap-northeast-3a', 'ap-northeast-3b', 'ap-northeast-3c']
  },
  {
    destVpcRegion: 'ap-south-1',
    zones: ['ap-south-1a', 'ap-south-1b', 'ap-south-1c']
  },
  {
    destVpcRegion: 'ap-southeast-1',
    zones: ['ap-southeast-1a', 'ap-southeast-1b', 'ap-southeast-1c']
  },
  {
    destVpcRegion: 'ap-southeast-2',
    zones: ['ap-southeast-2a', 'ap-southeast-2b', 'ap-southeast-2c']
  },
  {
    destVpcRegion: 'ap-southeast-3',
    zones: ['ap-southeast-3a', 'ap-southeast-3b', 'ap-southeast-3c']
  },
  {
    destVpcRegion: 'ca-central-1',
    zones: ['ca-central-1a', 'ca-central-1b']
  },
  {
    destVpcRegion: 'eu-central-1',
    zones: ['eu-central-1a', 'eu-central-1b', 'eu-central-1c']
  },
  {
    destVpcRegion: 'eu-west-1',
    zones: ['eu-west-1a', 'eu-west-1b', 'eu-west-1c']
  },
  {
    destVpcRegion: 'eu-west-2',
    zones: ['eu-west-2a', 'eu-west-2b', 'eu-west-2c']
  },
  {
    destVpcRegion: 'eu-west-3',
    zones: ['eu-west-3a', 'eu-west-3b', 'eu-west-3c']
  },
  {
    destVpcRegion: 'sa-east-1',
    zones: ['sa-east-1a', 'sa-east-1b', 'sa-east-1c']
  },
  {
    destVpcRegion: 'us-east-1',
    zones: ['us-east-1a', 'us-east-1b', 'us-east-1c', 'us-east-1d', 'us-east-1e', 'us-east-1f']
  },
  {
    destVpcRegion: 'us-east-2',
    zones: ['us-east-2a', 'us-east-2b', 'us-east-2c']
  },
  {
    destVpcRegion: 'us-west-1',
    zones: ['us-west-1a', 'us-west-1b']
  },
  {
    destVpcRegion: 'us-west-2',
    zones: ['us-west-2a', 'us-west-2b', 'us-west-2c']
  },
  {
    destVpcRegion: 'af-south-1',
    zones: ['af-south-1a', 'af-south-1b', 'af-south-1c']
  },
  {
    destVpcRegion: 'me-south-1',
    zones: ['me-south-1a', 'me-south-1b', 'me-south-1c']
  },
  {
    destVpcRegion: 'ap-east-1',
    zones: ['ap-east-1a', 'ap-east-1b', 'ap-east-1c']
  },
  {
    destVpcRegion: 'eu-south-1',
    zones: ['eu-south-1a', 'eu-south-1b', 'eu-south-1c']
  },
  {
    destVpcRegion: 'eu-north-1',
    zones: ['eu-north-1a', 'eu-north-1b', 'eu-north-1c']
  },
  {
    destVpcRegion: 'us-gov-east-1',
    zones: ['us-gov-east-1a', 'us-gov-east-1b', 'us-gov-east-1c']
  },
  {
    destVpcRegion: 'us-gov-west-1',
    zones: ['us-gov-west-1a', 'us-gov-west-1b', 'us-gov-west-1c']
  }
];

export const GCP_KMS_REGIONS = [
  {
    label: 'Global',
    options: [{ value: 'global', label: 'Global' }]
  },
  {
    label: 'Single Region',
    options: [
      { value: 'asia-east1', label: 'Taiwan (asia-east1)' },
      { value: 'asia-east2', label: 'Hong Kong (asia-east2)' },
      { value: 'asia-northeast1', label: 'Tokyo (asia-northeast1)' },
      { value: 'asia-northeast2', label: 'Osaka (asia-northeast2)' },
      { value: 'asia-northeast3', label: 'Seoul (asia-northeast3)' },
      { value: 'asia-south1', label: 'Mumbai (asia-south1)' },
      { value: 'asia-south2', label: 'Delhi (asia-south2)' },
      { value: 'asia-southeast1', label: 'Singapore (asia-southeast1)' },
      { value: 'asia-southeast2', label: 'Jakarta (asia-southeast2)' },
      { value: 'australia-southeast1', label: 'Sydney (australia-southeast1)' },
      { value: 'australia-southeast2', label: 'Melbourne (australia-southeast2)' },
      { value: 'europe-central2', label: 'Warsaw (europe-central2)' },
      { value: 'europe-north1', label: 'Finland (europe-north1)' },
      { value: 'europe-west1', label: 'Belgium (europe-west1)' },
      { value: 'europe-west2', label: 'London (europe-west2)' },
      { value: 'europe-west3', label: 'Frankfurt (europe-west3)' },
      { value: 'europe-west4', label: 'Netherlands (europe-west4)' },
      { value: 'europe-west6', label: 'Zurich (europe-west6)' },
      { value: 'northamerica-northeast1', label: 'Montreal (northamerica-northeast1)' },
      { value: 'northamerica-northeast2', label: 'Toronto (northamerica-northeast2)' },
      { value: 'southamerica-east1', label: 'Sao Paulo (southamerica-east1)' },
      { value: 'us-central1', label: 'Iowa (us-central1)' },
      { value: 'us-east1', label: 'South Carolina (us-east1)' },
      { value: 'us-east4', label: 'N. Virginia (us-east4)' },
      { value: 'us-east5', label: 'Ohio (us-east5)' },
      { value: 'us-south1', label: 'Texas (us-south1)' },
      { value: 'us-west1', label: 'Oregon (us-west1)' },
      { value: 'us-west2', label: 'Los Angeles (us-west2)' },
      { value: 'us-west3', label: 'Salt Lake City (us-west3)' },
      { value: 'us-west4', label: 'Las Vegas (us-west4)' }
    ]
  },
  {
    label: 'Multi Region',
    options: [
      { value: 'asia1', label: 'Tokyo, Osaka and Seoul (asia1)' },
      { value: 'us', label: 'Multiple regions in the United States (us)' },
      { value: 'europe', label: 'Multiple regions in the European Union (europe)' }
    ]
  }
];

export const GCP_KMS_REGIONS_FLATTENED = GCP_KMS_REGIONS.reduce((regionArray, region) => {
  regionArray = [...regionArray, ...region.options];
  return regionArray;
}, []);
