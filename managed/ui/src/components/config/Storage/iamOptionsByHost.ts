// Copyright (c) YugabyteDB, Inc.

import { YBAHost } from '../../../redesign/helpers/constants';

type YBAHostValue = typeof YBAHost[keyof typeof YBAHost];

// The two options that are only YBA's own access to the bucket: an S3 IAM role, whose bucket check
// on save uses YBA's AWS credentials, and GCS federation, an AWS-sourced credential for YBA. Neither
// can work with YBA on GCP. The other IAM options are also the DB nodes' access (S3 federation, GKE
// workload identity), and the backend defers to the nodes when YBA cannot use them, so they stay.
// A saved config shows every option. A new one keeps an option visible while it is on, so one
// turned on before the host was known can still be turned off.

export const getS3IamOptions = (ybaHost: YBAHostValue, isEdited: boolean, iamRoleOn: boolean) => ({
  showIamRole: isEdited || iamRoleOn || ybaHost !== YBAHost.GCP
});

export const getGcsIamOptions = (
  ybaHost: YBAHostValue,
  isEdited: boolean,
  federatedIamOn: boolean
) => ({
  showFederatedIam: isEdited || federatedIamOn || ybaHost !== YBAHost.GCP
});
