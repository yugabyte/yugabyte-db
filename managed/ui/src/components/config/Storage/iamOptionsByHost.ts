// Copyright (c) YugabyteDB, Inc.

import { YBAHost } from '../../../redesign/helpers/constants';

type YBAHostValue = typeof YBAHost[keyof typeof YBAHost];

// Each IAM option works from one YBA host only: the native one on its own cloud, federation from
// the other. An option that is already on stays visible, so a saved config shows what it uses.
// Any other host, or unknown, keeps every option.

export const getS3IamOptions = (
  ybaHost: YBAHostValue,
  iamRoleEnabled: boolean,
  federatedIamEnabled: boolean
) => ({
  showIamRole: ybaHost !== YBAHost.GCP || iamRoleEnabled,
  showFederatedIam: ybaHost !== YBAHost.AWS || federatedIamEnabled
});

export const getGcsIamOptions = (
  ybaHost: YBAHostValue,
  useGcpIam: boolean,
  federatedIamEnabled: boolean
) => ({
  showGcpIam: ybaHost !== YBAHost.AWS || useGcpIam,
  showFederatedIam: ybaHost !== YBAHost.GCP || federatedIamEnabled
});
