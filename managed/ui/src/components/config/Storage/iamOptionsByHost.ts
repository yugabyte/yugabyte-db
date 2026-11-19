// Copyright (c) YugabyteDB, Inc.

import { YBAHost } from '../../../redesign/helpers/constants';

type YBAHostValue = (typeof YBAHost)[keyof typeof YBAHost];

// Only while federated IAM is offered, a new config shows the one IAM option that works from where
// YBA runs: on AWS, S3 IAM Role and GCS Federated IAM; on GCP, S3 Federated IAM and GCS GCP IAM.
// GCS Federated IAM, which needs AWS instance metadata, is also hidden on Azure; a self-hosted or
// unknown host hides nothing. A saved config, or an option that is on, still shows it.

export const getS3IamOptions = (
  ybaHost: YBAHostValue,
  isEdited: boolean,
  iamRoleOn: boolean,
  federatedIamOn: boolean,
  federatedIamOffered: boolean
) => ({
  showIamRole: isEdited || iamRoleOn || !federatedIamOffered || ybaHost !== YBAHost.GCP,
  showFederatedIam: isEdited || federatedIamOn || ybaHost !== YBAHost.AWS
});

export const getGcsIamOptions = (
  ybaHost: YBAHostValue,
  isEdited: boolean,
  gcpIamOn: boolean,
  federatedIamOn: boolean,
  federatedIamOffered: boolean
) => ({
  showGcpIam: isEdited || gcpIamOn || !federatedIamOffered || ybaHost !== YBAHost.AWS,
  showFederatedIam:
    isEdited || federatedIamOn || ybaHost === YBAHost.AWS || ybaHost === YBAHost.SELF_HOSTED
});
