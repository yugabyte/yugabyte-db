// Copyright (c) YugabyteDB, Inc.

import { describe, expect, it } from 'vitest';
import { YBAHost } from '../../../redesign/helpers/constants';
import { getGcsIamOptions, getS3IamOptions, hostBaseNamesRegion } from './iamOptionsByHost';

const ALL_HOSTS = [YBAHost.AWS, YBAHost.GCP, YBAHost.AZU, YBAHost.SELF_HOSTED];

// New config, nothing turned on, federated IAM offered.
const s3 = (host: (typeof ALL_HOSTS)[number]) => getS3IamOptions(host, false, false, true);
const gcs = (host: (typeof ALL_HOSTS)[number]) => getGcsIamOptions(host, false, false, false, true);

describe('getS3IamOptions', () => {
  it('shows the IAM role and federated IAM, which works from both clouds, on a new config on AWS', () => {
    expect(s3(YBAHost.AWS)).toEqual({ showIamRole: true, showFederatedIam: true });
  });

  it('shows only federated IAM on a new config on GCP, where YBA has no AWS credentials', () => {
    expect(s3(YBAHost.GCP)).toEqual({ showIamRole: false, showFederatedIam: true });
  });

  it('hides nothing on Azure or a self-hosted or unknown host', () => {
    for (const host of [YBAHost.AZU, YBAHost.SELF_HOSTED]) {
      expect(s3(host)).toEqual({ showIamRole: true, showFederatedIam: true });
    }
  });

  it('shows every option when editing, and the IAM role while it is on', () => {
    for (const host of ALL_HOSTS) {
      expect(getS3IamOptions(host, true, false, true)).toEqual({
        showIamRole: true,
        showFederatedIam: true
      });
    }
    expect(getS3IamOptions(YBAHost.GCP, false, true, true).showIamRole).toBe(true);
  });

  it('keeps the IAM role on every host while federated IAM is not offered', () => {
    for (const host of ALL_HOSTS) {
      expect(getS3IamOptions(host, false, false, false).showIamRole).toBe(true);
    }
  });
});

describe('getGcsIamOptions', () => {
  it('shows only federated IAM, which works from both clouds, on a new config on AWS or GCP', () => {
    for (const host of [YBAHost.AWS, YBAHost.GCP]) {
      expect(gcs(host)).toEqual({ showGcpIam: false, showFederatedIam: true });
    }
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
    expect(getGcsIamOptions(YBAHost.GCP, false, true, false, true).showGcpIam).toBe(true);
    expect(getGcsIamOptions(YBAHost.AZU, false, false, true, true).showFederatedIam).toBe(true);
  });

  it('keeps GCP IAM on every host while federated IAM is not offered', () => {
    for (const host of ALL_HOSTS) {
      expect(getGcsIamOptions(host, false, false, false, false).showGcpIam).toBe(true);
    }
  });
});

describe('hostBaseNamesRegion', () => {
  it('reads the region off a regional AWS host base, as the backend does', () => {
    for (const hostBase of [
      's3.ap-south-1.amazonaws.com',
      's3-us-west-2.amazonaws.com',
      's3.dualstack.eu-west-1.amazonaws.com',
      's3.us-gov-west-1.amazonaws.com'
    ]) {
      expect(hostBaseNamesRegion(hostBase)).toBe(true);
    }
  });

  it('finds no region in the default host base, a blank one, or another endpoint', () => {
    for (const hostBase of [undefined, '', 's3.amazonaws.com', 'minio.example.com']) {
      expect(hostBaseNamesRegion(hostBase)).toBe(false);
    }
  });
});
