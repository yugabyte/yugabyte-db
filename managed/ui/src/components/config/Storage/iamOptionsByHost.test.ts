// Copyright (c) YugabyteDB, Inc.

import { describe, expect, it } from 'vitest';
import { YBAHost } from '../../../redesign/helpers/constants';
import { getGcsIamOptions, getS3IamOptions } from './iamOptionsByHost';

describe('getS3IamOptions', () => {
  it('hides the IAM role only when YBA runs on GCP', () => {
    expect(getS3IamOptions(YBAHost.GCP, false).showIamRole).toBe(false);
    for (const host of [YBAHost.AWS, YBAHost.AZU, YBAHost.SELF_HOSTED]) {
      expect(getS3IamOptions(host, false).showIamRole).toBe(true);
    }
  });

  it('shows the IAM role when editing a saved config', () => {
    expect(getS3IamOptions(YBAHost.GCP, true).showIamRole).toBe(true);
  });
});

describe('getGcsIamOptions', () => {
  it('offers only GCP IAM on GCP and only federation on AWS', () => {
    expect(getGcsIamOptions(YBAHost.GCP, false)).toEqual({
      showGcpIam: true,
      showFederatedIam: false
    });
    expect(getGcsIamOptions(YBAHost.AWS, false)).toEqual({
      showGcpIam: false,
      showFederatedIam: true
    });
  });

  it('offers both on any other or unknown host', () => {
    for (const host of [YBAHost.AZU, YBAHost.SELF_HOSTED]) {
      expect(getGcsIamOptions(host, false)).toEqual({
        showGcpIam: true,
        showFederatedIam: true
      });
    }
  });

  it('offers both when editing a saved config', () => {
    for (const host of [YBAHost.AWS, YBAHost.GCP]) {
      expect(getGcsIamOptions(host, true)).toEqual({
        showGcpIam: true,
        showFederatedIam: true
      });
    }
  });
});
