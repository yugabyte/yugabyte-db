// Copyright (c) YugabyteDB, Inc.

import { YBAHost } from '../../../redesign/helpers/constants';

type YBAHostValue = (typeof YBAHost)[keyof typeof YBAHost];

// Only while federated IAM is offered, a new config on a YBA that runs on AWS or GCP shows one IAM
// option per page: Federated IAM, for S3 and for GCS alike. It is the option that works from both
// clouds, which an HA pair across AWS and GCP needs: the standby, once promoted, reaches the bucket
// through the provider's federated IAM entry. S3 IAM Role works only from AWS, where YBA hands its
// own credentials to the DB nodes, and GCS GCP IAM only from GCP, so both are hidden there. With
// Federated IAM, a YBA on the bucket's cloud still uses its own identity, and the DB nodes their
// own: an AWS node its instance role, so that role needs access to the S3 bucket. Editing a saved
// config, where every option shows, or the API still sets either. GCS Federated IAM, which needs
// AWS instance metadata, is hidden on Azure; a self-hosted or unknown host hides nothing. An option
// that is on still shows.

const runsOnAwsOrGcp = (ybaHost: YBAHostValue) =>
  ybaHost === YBAHost.AWS || ybaHost === YBAHost.GCP;

export const getS3IamOptions = (
  ybaHost: YBAHostValue,
  isEdited: boolean,
  iamRoleOn: boolean,
  federatedIamOffered: boolean
) => ({
  showIamRole: isEdited || iamRoleOn || !federatedIamOffered || !runsOnAwsOrGcp(ybaHost),
  showFederatedIam: true
});

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
