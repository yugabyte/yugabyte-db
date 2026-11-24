// Copyright (c) YugabyteDB, Inc.

import { YBAHost } from '../../../redesign/helpers/constants';

type YBAHostValue = (typeof YBAHost)[keyof typeof YBAHost];

// Only while federated IAM is offered, a new config on a YBA that runs on AWS or GCP shows the IAM
// options that work from there. Federated IAM works from both clouds, which an HA pair across AWS
// and GCP needs: the standby, once promoted, reaches the bucket through the provider's federated
// IAM entry. With it, a YBA on the bucket's cloud still uses its own identity, and the DB nodes
// their own: an AWS node its instance role, so that role needs access to the S3 bucket. S3 IAM
// Role, where YBA hands its own credentials to the DB nodes, also shows on AWS and is hidden on
// GCP, where YBA has none to hand; it keeps its bucket region lookup and its bucket checks at save,
// which Federated IAM skips. GCS GCP IAM works only from GCP and is hidden on both, so a keyless
// GCS config is reachable from the standby too. Editing a saved config, where every option shows,
// or the API still sets either. GCS Federated IAM, which needs AWS instance metadata, is hidden on
// Azure; a self-hosted or unknown host hides nothing. An option that is on still shows.

const runsOnAwsOrGcp = (ybaHost: YBAHostValue) =>
  ybaHost === YBAHost.AWS || ybaHost === YBAHost.GCP;

export const getS3IamOptions = (
  ybaHost: YBAHostValue,
  isEdited: boolean,
  iamRoleOn: boolean,
  federatedIamOffered: boolean
) => ({
  showIamRole: isEdited || iamRoleOn || !federatedIamOffered || ybaHost !== YBAHost.GCP,
  showFederatedIam: true
});

// Mirrors AWSUtil's AWS_HOST_BASE_REGION_PATTERN. YBA does not look up the bucket region of a
// federated S3 config: the backend takes its signing region, otherwise the one this names,
// otherwise us-east-1, with which a bucket elsewhere fails with an opaque HTTP 301.
const HOST_BASE_REGION = /^s3[.-](?:[a-z0-9-]+[.])*([a-z]{2}-[a-z-]+-\d+)[.]amazonaws[.]com$/;

export const hostBaseNamesRegion = (hostBase?: string) =>
  !!hostBase && HOST_BASE_REGION.test(hostBase);

export const getGcsIamOptions = (
  ybaHost: YBAHostValue,
  isEdited: boolean,
  gcpIamOn: boolean,
  federatedIamOn: boolean,
  federatedIamOffered: boolean
) => ({
  showGcpIam: isEdited || gcpIamOn || !federatedIamOffered || !runsOnAwsOrGcp(ybaHost),
  showFederatedIam: isEdited || federatedIamOn || ybaHost !== YBAHost.AZU
});
