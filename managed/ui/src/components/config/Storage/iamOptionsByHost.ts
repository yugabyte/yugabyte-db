// Copyright (c) YugabyteDB, Inc.

import { YBAHost } from '../../../redesign/helpers/constants';

type YBAHostValue = (typeof YBAHost)[keyof typeof YBAHost];

// Only while federated IAM is offered, a new config shows the IAM options that work from where YBA
// runs. Editing a saved config shows every option, an option that is on always shows, and the API
// still sets either.
// S3: Federated IAM on every host, and IAM Role on every host but GCP. IAM Role uses AWS credentials
// from YBA's host, which a YBA on GCP has only if its host is set up with some. Federated IAM works
// from both clouds, as the standby of an HA pair across AWS and GCP needs once promoted. With it,
// an AWS DB node uses its instance role, which then needs access to the bucket. IAM Role is still
// offered for its bucket region lookup and its bucket checks at save, which Federated IAM skips.
// GCS: GCP IAM on every host but AWS, and Federated IAM on AWS and on a self-hosted or unknown
// host: not on GCP, where it would skip YBA's own bucket checks, nor on Azure, as it needs AWS
// instance metadata.

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

type S3FormValues = { FEDERATED_IAM?: boolean; AWS_HOST_BASE?: string; SIGNING_REGION?: string };

const isBlank = (value?: string) => !value?.trim();

// Field-level validator for SIGNING_REGION (redux-form passes the value, the form values and the
// form props). YBA does not look up a federated config's bucket region, so a federated config
// names it unless the host base does. On an edit the host base is fixed, and an IAM Role config
// never stored a region. Only a config saved federated without a region keeps taking edits
// without one; one saved with a region cannot drop it.
export const regionRequiredForFederatedIam = (
  value: string | undefined,
  data?: S3FormValues,
  props?: { initialValues?: S3FormValues }
) => {
  const saved = props?.initialValues;
  const savedFederatedWithoutRegion = !!saved?.FEDERATED_IAM && isBlank(saved?.SIGNING_REGION);
  return data?.FEDERATED_IAM &&
    isBlank(value) &&
    !savedFederatedWithoutRegion &&
    !hostBaseNamesRegion(data?.AWS_HOST_BASE)
    ? 'Required for Federated IAM, unless the host base names the region.'
    : undefined;
};

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
