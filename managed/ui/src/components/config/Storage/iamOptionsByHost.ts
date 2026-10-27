// Copyright (c) YugabyteDB, Inc.

import { YBAHost } from '../../../redesign/helpers/constants';

type YBAHostValue = typeof YBAHost[keyof typeof YBAHost];

// A heuristic for the default setup, matching the provider page, which offers the AWS IAM instance
// profile only when YBA runs on AWS. With YBA on GCP, a new config does not offer:
// - S3 IAM Role: YBA's AWS credentials normally come from its instance profile. It can still work
//   from a GCP host through an AWS profile or web-identity token there, or the DB nodes' own roles,
//   so a saved config that uses it keeps showing it.
// - GCS Federated IAM: an AWS-sourced credential for YBA itself, which cannot work on GCP.
// The other IAM options are also the DB nodes' access (S3 federation, GKE workload identity), and
// the backend defers to the nodes when YBA cannot use them, so they stay on every host.
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
