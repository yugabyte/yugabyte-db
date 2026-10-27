// Copyright (c) YugabyteDB, Inc.

import { YBAHost } from '../../../redesign/helpers/constants';

type YBAHostValue = typeof YBAHost[keyof typeof YBAHost];

// These options are YBA's own access to the bucket, which YBA validates when the config is saved,
// so each one works from a single YBA host. S3 federated IAM is not here: it is the GCP DB nodes'
// access, which YBA never uses itself. A saved config shows every option, so editing changes
// nothing. Any other host, or unknown, keeps every option.

export const getS3IamOptions = (ybaHost: YBAHostValue, isEdited: boolean) => ({
  showIamRole: isEdited || ybaHost !== YBAHost.GCP
});

export const getGcsIamOptions = (ybaHost: YBAHostValue, isEdited: boolean) => ({
  showGcpIam: isEdited || ybaHost !== YBAHost.AWS,
  showFederatedIam: isEdited || ybaHost !== YBAHost.GCP
});
