// Copyright (c) YugabyteDB, Inc.

import { describe, expect, it } from 'vitest';
import { YBAHost } from '../../../redesign/helpers/constants';
import { getGcsIamOptions, getS3IamOptions } from './iamOptionsByHost';

const OTHER_HOSTS = [YBAHost.AWS, YBAHost.AZU, YBAHost.SELF_HOSTED];

describe('getS3IamOptions', () => {
  it('hides the IAM role on a new config only when YBA runs on GCP', () => {
    expect(getS3IamOptions(YBAHost.GCP, false, false, true).showIamRole).toBe(false);
    for (const host of OTHER_HOSTS) {
      expect(getS3IamOptions(host, false, false, true).showIamRole).toBe(true);
    }
  });

  it('shows the IAM role when editing, or while it is on', () => {
    expect(getS3IamOptions(YBAHost.GCP, true, false, true).showIamRole).toBe(true);
    expect(getS3IamOptions(YBAHost.GCP, false, true, true).showIamRole).toBe(true);
  });

  it('keeps the IAM role on GCP while federated IAM is not offered', () => {
    expect(getS3IamOptions(YBAHost.GCP, false, false, false).showIamRole).toBe(true);
  });
});

describe('getGcsIamOptions', () => {
  it('hides federated IAM on a new config when YBA runs on GCP or Azure', () => {
    expect(getGcsIamOptions(YBAHost.GCP, false, false).showFederatedIam).toBe(false);
    expect(getGcsIamOptions(YBAHost.AZU, false, false).showFederatedIam).toBe(false);
  });

  it('keeps federated IAM on AWS and on a self-hosted or unknown host', () => {
    expect(getGcsIamOptions(YBAHost.AWS, false, false).showFederatedIam).toBe(true);
    expect(getGcsIamOptions(YBAHost.SELF_HOSTED, false, false).showFederatedIam).toBe(true);
  });

  it('shows federated IAM when editing, or while it is on', () => {
    for (const host of [YBAHost.GCP, YBAHost.AZU]) {
      expect(getGcsIamOptions(host, true, false).showFederatedIam).toBe(true);
      expect(getGcsIamOptions(host, false, true).showFederatedIam).toBe(true);
    }
  });
});
