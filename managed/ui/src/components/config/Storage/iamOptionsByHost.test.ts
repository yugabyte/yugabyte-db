// Copyright (c) YugabyteDB, Inc.

import { describe, expect, it } from 'vitest';
import { YBAHost } from '../../../redesign/helpers/constants';
import { getGcsIamOptions, getS3IamOptions } from './iamOptionsByHost';

describe('getS3IamOptions', () => {
  it('offers only the IAM role on AWS and only federation on GCP', () => {
    expect(getS3IamOptions(YBAHost.AWS, false, false)).toEqual({
      showIamRole: true,
      showFederatedIam: false
    });
    expect(getS3IamOptions(YBAHost.GCP, false, false)).toEqual({
      showIamRole: false,
      showFederatedIam: true
    });
  });

  it('offers both on any other or unknown host', () => {
    for (const host of [YBAHost.AZU, YBAHost.SELF_HOSTED]) {
      expect(getS3IamOptions(host, false, false)).toEqual({
        showIamRole: true,
        showFederatedIam: true
      });
    }
  });

  it('keeps an option that is already on under the opposite host', () => {
    expect(getS3IamOptions(YBAHost.GCP, true, false).showIamRole).toBe(true);
    expect(getS3IamOptions(YBAHost.AWS, false, true).showFederatedIam).toBe(true);
  });
});

describe('getGcsIamOptions', () => {
  it('offers only GCP IAM on GCP and only federation on AWS', () => {
    expect(getGcsIamOptions(YBAHost.GCP, false, false)).toEqual({
      showGcpIam: true,
      showFederatedIam: false
    });
    expect(getGcsIamOptions(YBAHost.AWS, false, false)).toEqual({
      showGcpIam: false,
      showFederatedIam: true
    });
  });

  it('offers both on any other or unknown host', () => {
    for (const host of [YBAHost.AZU, YBAHost.SELF_HOSTED]) {
      expect(getGcsIamOptions(host, false, false)).toEqual({
        showGcpIam: true,
        showFederatedIam: true
      });
    }
  });

  it('keeps an option that is already on under the opposite host', () => {
    expect(getGcsIamOptions(YBAHost.AWS, true, false).showGcpIam).toBe(true);
    expect(getGcsIamOptions(YBAHost.GCP, false, true).showFederatedIam).toBe(true);
  });
});
