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
  it('hides federated IAM on a new config only when YBA runs on GCP', () => {
    expect(getGcsIamOptions(YBAHost.GCP, false, false).showFederatedIam).toBe(false);
    for (const host of OTHER_HOSTS) {
      expect(getGcsIamOptions(host, false, false).showFederatedIam).toBe(true);
    }
  });

  it('shows federated IAM when editing, or while it is on', () => {
    expect(getGcsIamOptions(YBAHost.GCP, true, false).showFederatedIam).toBe(true);
    expect(getGcsIamOptions(YBAHost.GCP, false, true).showFederatedIam).toBe(true);
  });
});
