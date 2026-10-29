import { describe, expect, it } from 'vitest';

import {
  buildFederationPayload,
  federationFormValuesFromProvider,
  parseGcsCredentialConfig
} from './CrossCloudFederatedIamFields';

const AUDIENCE =
  '//iam.googleapis.com/projects/123/locations/global/workloadIdentityPools/pool/providers/aws';

const credentialConfig = (overrides: Record<string, unknown> = {}) =>
  JSON.stringify({
    universe_domain: 'googleapis.com',
    type: 'external_account',
    audience: AUDIENCE,
    subject_token_type: 'urn:ietf:params:aws:token-type:aws4_request',
    token_url: 'https://sts.googleapis.com/v1/token',
    credential_source: { environment_id: 'aws1' },
    ...overrides
  });

describe('parseGcsCredentialConfig', () => {
  it('returns the audience of a workload identity credential configuration', () => {
    expect(parseGcsCredentialConfig(credentialConfig())).toEqual({ audience: AUDIENCE });
  });

  it('rejects a file that is not JSON', () => {
    expect(parseGcsCredentialConfig('not json').error).toMatch(/not valid JSON/);
    expect(parseGcsCredentialConfig(null).error).toMatch(/not valid JSON/);
  });

  it('rejects a service account key file', () => {
    expect(parseGcsCredentialConfig(credentialConfig({ type: 'service_account' })).error).toMatch(
      /not a workload identity credential configuration/
    );
  });

  it('rejects service account impersonation', () => {
    const text = credentialConfig({
      service_account_impersonation_url:
        'https://iamcredentials.googleapis.com/v1/projects/-/serviceAccounts/sa@p.iam.gserviceaccount.com:generateAccessToken'
    });
    expect(parseGcsCredentialConfig(text).error).toMatch(/impersonation is not supported/);
  });

  it('rejects a missing or malformed audience', () => {
    expect(parseGcsCredentialConfig(credentialConfig({ audience: undefined })).error).toMatch(
      /no valid audience/
    );
    expect(parseGcsCredentialConfig(credentialConfig({ audience: 'has "quotes"' })).error).toMatch(
      /no valid audience/
    );
  });
});

describe('buildFederationPayload', () => {
  it('turns federation on when a storage cloud is on', () => {
    expect(
      buildFederationPayload({ federationGcsEnabled: true, federationGcsAudience: AUDIENCE })
    ).toEqual({
      enableFederatedIam: true,
      crossCloudFederationTargets: [{ targetCloud: 'gcp', audience: AUDIENCE }]
    });
  });

  it('turns federation off explicitly when every storage cloud is off', () => {
    expect(
      buildFederationPayload({
        federationGcsEnabled: false,
        federationGcsAudience: AUDIENCE,
        federationS3Enabled: false
      })
    ).toEqual({ enableFederatedIam: false });
  });
});

describe('federationFormValuesFromProvider', () => {
  const targets = [{ targetCloud: 'gcp', audience: AUDIENCE }];

  it('shows saved targets as turned on', () => {
    const values = federationFormValuesFromProvider(
      { enableFederatedIam: true, crossCloudFederationTargets: targets },
      'aws'
    );
    expect(values.federationGcsEnabled).toBe(true);
    expect(values.federationGcsAudience).toBe(AUDIENCE);
  });

  it('keeps clouds off when federation is off, so saving does not turn it on', () => {
    const values = federationFormValuesFromProvider(
      { enableFederatedIam: false, crossCloudFederationTargets: targets },
      'aws'
    );
    expect(values.federationGcsEnabled).toBe(false);
    expect(buildFederationPayload(values)).toEqual({ enableFederatedIam: false });
  });
});
