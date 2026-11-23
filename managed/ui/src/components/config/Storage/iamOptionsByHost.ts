// Copyright (c) YugabyteDB, Inc.

import { YBAHost } from '../../../redesign/helpers/constants';

type YBAHostValue = (typeof YBAHost)[keyof typeof YBAHost];

// Only while federated IAM is offered, a new config shows one IAM option per page by YBA's host: on
// AWS, S3 IAM Role and GCS Federated IAM; on GCP, S3 Federated IAM and GCS GCP IAM. On AWS this
// hides, on purpose, S3 Federated IAM, the GCP DB nodes' keyless path to S3, and GCS GCP IAM, so a
// keyless GCS config also has YBA reach the bucket with its own AWS role, which the GCP provider's
// identity pool must admit. Editing a saved config, where every option shows, or the API still
// sets either. GCS Federated IAM, which needs AWS instance metadata, is also hidden on Azure; a
// self-hosted or unknown host hides nothing. An option that is on still shows.

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
