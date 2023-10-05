// These should match the metadata in devops under opscli/ybops/data/aws-metadata.yml
export const regionsData = [
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
    destVpcRegion: 'ap-south-1',
    zones: ['ap-south-1a', 'ap-south-1b', 'ap-south-1c']
  },
  {
    destVpcRegion: 'ap-south-2',
    zones: ['ap-south-2a', 'ap-south-2b', 'ap-south-2c']
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
    destVpcRegion: 'ca-central-1',
    zones: ['ca-central-1a', 'ca-central-1b', 'ca-central-1c', 'ca-central-1d']
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
    zones: ['us-east-1a', 'us-east-1b', 'us-east-1c', 'us-east-1d', 'us-east-1e', 'us-east-1f', 'us-east-1-atl-1a']
  },
  {
    destVpcRegion: 'us-east-2',
    zones: ['us-east-2a', 'us-east-2b', 'us-east-2c']
  },
  {
    destVpcRegion: 'us-west-1',
    zones: ['us-west-1a', 'us-west-1b', 'us-west1-c']
  },
  {
    destVpcRegion: 'us-west-2',
    zones: ['us-west-2a', 'us-west-2b', 'us-west-2c', 'us-west-2d', 'us-west-2-den-1a', 'us-west-2-las-1a', 'us-west-2-lax-1a', 'us-west-2-lax-1b', 'us-west-2-sea-1a']
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
  }
];
