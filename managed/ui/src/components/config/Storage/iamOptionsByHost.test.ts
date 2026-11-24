// Copyright (c) YugabyteDB, Inc.

import { describe, expect, it } from 'vitest';
import { YBAHost } from '../../../redesign/helpers/constants';
import { getGcsIamOptions, getS3IamOptions } from './iamOptionsByHost';

const ALL_HOSTS = [YBAHost.AWS, YBAHost.GCP, YBAHost.AZU, YBAHost.SELF_HOSTED];

// New config, nothing turned on, federated IAM offered.
const s3 = (host: (typeof ALL_HOSTS)[number]) => getS3IamOptions(host, false, false, false, true);
const gcs = (host: (typeof ALL_HOSTS)[number]) => getGcsIamOptions(host, false, false, false, true);

describe('getS3IamOptions', () => {
  it('shows only the option that works from where YBA runs on a new config', () => {
    expect(s3(YBAHost.AWS)).toEqual({ showIamRole: true, showFederatedIam: false });
    expect(s3(YBAHost.GCP)).toEqual({ showIamRole: false, showFederatedIam: true });
  });

  it('hides nothing on Azure or a self-hosted or unknown host', () => {
    for (const host of [YBAHost.AZU, YBAHost.SELF_HOSTED]) {
      expect(s3(host)).toEqual({ showIamRole: true, showFederatedIam: true });
    }
  });

  it('shows every option when editing, and an option while it is on', () => {
    for (const host of ALL_HOSTS) {
      expect(getS3IamOptions(host, true, false, false, true)).toEqual({
        showIamRole: true,
        showFederatedIam: true
      });
    }
    expect(getS3IamOptions(YBAHost.GCP, false, true, false, true).showIamRole).toBe(true);
    expect(getS3IamOptions(YBAHost.AWS, false, false, true, true).showFederatedIam).toBe(true);
  });

  it('keeps the IAM role on every host while federated IAM is not offered', () => {
    for (const host of ALL_HOSTS) {
      expect(getS3IamOptions(host, false, false, false, false).showIamRole).toBe(true);
    }
  });
});

describe('getGcsIamOptions', () => {
  it('shows only the option that works from where YBA runs on a new config', () => {
    expect(gcs(YBAHost.AWS)).toEqual({ showGcpIam: false, showFederatedIam: true });
    expect(gcs(YBAHost.GCP)).toEqual({ showGcpIam: true, showFederatedIam: false });
  });

  it('hides federated IAM on Azure, and nothing on a self-hosted or unknown host', () => {
    expect(gcs(YBAHost.AZU)).toEqual({ showGcpIam: true, showFederatedIam: false });
    expect(gcs(YBAHost.SELF_HOSTED)).toEqual({ showGcpIam: true, showFederatedIam: true });
  });

  it('shows every option when editing, and an option while it is on', () => {
    for (const host of ALL_HOSTS) {
      expect(getGcsIamOptions(host, true, false, false, true)).toEqual({
        showGcpIam: true,
        showFederatedIam: true
      });
    }
    expect(getGcsIamOptions(YBAHost.AWS, false, true, false, true).showGcpIam).toBe(true);
    expect(getGcsIamOptions(YBAHost.GCP, false, false, true, true).showFederatedIam).toBe(true);
  });

  it('keeps GCP IAM on every host while federated IAM is not offered', () => {
    for (const host of ALL_HOSTS) {
      expect(getGcsIamOptions(host, false, false, false, false).showGcpIam).toBe(true);
    }
  });
});
