// Copyright (c) YugabyteDB, Inc.

import { YBAHost } from '../../../redesign/helpers/constants';

type YBAHostValue = (typeof YBAHost)[keyof typeof YBAHost];

// Only while federated IAM is offered, a new config shows the IAM options that work from where YBA
// runs. S3: Federated IAM on every host, IAM Role also on AWS (hidden on GCP, where YBA has no AWS
// credentials to hand to the DB nodes). Federated IAM works from both clouds, which an HA pair
// across AWS and GCP needs: the standby, once promoted, reaches the bucket through the provider's
// federated IAM entry. With it, the DB nodes use their own identity: an AWS node its instance role,
// so that role needs access to the bucket. IAM Role keeps its bucket region lookup and its bucket
// checks at save, which Federated IAM skips. GCS: GCP IAM on GCP, where YBA checks the bucket with
// its own identity, and Federated IAM on AWS only (on GCP it would skip those checks). A YBA on AWS
// also federates for a GCP IAM config, such as one made on its HA peer on GCP
// (BackupHelper.federateGcpIamOnAws). Editing a saved
// config, where every option shows, or the API still sets either. GCS Federated IAM, which needs
// AWS instance metadata, is hidden on Azure; a self-hosted or unknown host hides nothing. An option
// that is on still shows.

export const getS3IamOptions = (
  ybaHost: YBAHostValue,
  isEdited: boolean,
  iamRoleOn: boolean,
  federatedIamOffered: boolean
) => ({
  showIamRole: isEdited || iamRoleOn || !federatedIamOffered || ybaHost !== YBAHost.GCP
});

// Mirrors AWSUtil's AWS_HOST_BASE_REGION_PATTERN. YBA does not look up the bucket region of a
// federated S3 config: the backend takes its signing region, otherwise the one this names,
// otherwise us-east-1, with which a bucket elsewhere fails with an opaque HTTP 301.
const HOST_BASE_REGION = /^s3[.-](?:[a-z0-9-]+[.])*([a-z]{2}-[a-z-]+-\d+)[.]amazonaws[.]com$/;

export const hostBaseNamesRegion = (hostBase?: string) =>
  !!hostBase && HOST_BASE_REGION.test(hostBase);

type S3FormValues = { FEDERATED_IAM?: boolean; AWS_HOST_BASE?: string };

// Field-level validator for SIGNING_REGION (redux-form passes the value, the form values and the
// form props). A config that Federated IAM is turned on for, new or edited, names the bucket's
// region unless the host base does: on an edit the host base is fixed, and an IAM Role config never
// stored a region. A config saved federated keeps taking edits without one.
export const regionRequiredForFederatedIam = (
  value: string | undefined,
  data?: S3FormValues,
  props?: { initialValues?: S3FormValues }
) =>
  data?.FEDERATED_IAM &&
  !props?.initialValues?.FEDERATED_IAM &&
  !value &&
  !hostBaseNamesRegion(data?.AWS_HOST_BASE)
    ? 'Required for Federated IAM, unless the host base names the region.'
    : undefined;

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
