// Copyright (c) YugabyteDB, Inc.

import { YBAHost } from '../../../redesign/helpers/constants';

type YBAHostValue = (typeof YBAHost)[keyof typeof YBAHost];

// A heuristic for the default setup. A new config does not offer:
// - S3 IAM Role, with YBA on GCP: a GCP-hosted YBA has no AWS instance profile, and its intended
//   path to S3 is the S3 Federated IAM option, which uses YBA's GCP identity. The role can still work
//   there through an AWS profile or web-identity token, or the DB nodes' own roles, so a saved config
//   that uses it keeps showing it. While federated IAM is turned off for this YBA, the role is the
//   only IAM option left, so it is kept.
// - GCS Federated IAM, with YBA on GCP or Azure: it is YBA's own access, and YBA gets its credential
//   from AWS instance metadata (federation/gcp-fed-creds.json.j2), so it works only on AWS. It stays on
//   a self-hosted YBA, because that is also what the page uses when host info is unavailable or
//   names no cloud, which can be a YBA on AWS.
// Azure and self-hosted YBA keep S3 IAM Role, unlike the AWS provider page, which offers the instance
// profile only on AWS: they have no federated alternative, and an AWS profile is their usual way to
// give YBA an IAM role.
// The other IAM options are also the DB nodes' access (S3 federation, GKE workload identity), and
// the backend defers to the nodes when YBA cannot use them, so they stay on every host.
// A saved config shows every option. A new one keeps an option visible while it is on, so one
// turned on before the host was known can still be turned off.

export const getS3IamOptions = (
  ybaHost: YBAHostValue,
  isEdited: boolean,
  iamRoleOn: boolean,
  federatedIamOffered: boolean
) => ({
  showIamRole: isEdited || iamRoleOn || !federatedIamOffered || ybaHost !== YBAHost.GCP
});

export const getGcsIamOptions = (
  ybaHost: YBAHostValue,
  isEdited: boolean,
  federatedIamOn: boolean
) => ({
  showFederatedIam:
    isEdited || federatedIamOn || ybaHost === YBAHost.AWS || ybaHost === YBAHost.SELF_HOSTED
});
