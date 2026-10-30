import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { ThemeProvider } from '@material-ui/core';
import i18n from 'i18next';
import { initReactI18next } from 'react-i18next';

import { mainTheme } from '../../../../theme/mainTheme';
import { CrossCloudFederationModal } from './CrossCloudFederationModal';
import en from '../../../../../translations/en.json';

// The real strings, so the banners and the provider link inside Trans are checked as rendered.
void i18n.use(initReactI18next).init({
  resources: { en },
  lng: 'en',
  interpolation: { escapeValue: false }
});

vi.mock('../../../../../v2/api/universe/universe', () => ({
  useManageCrossCloudFederation: () => ({ mutate: vi.fn(), isLoading: false })
}));
vi.mock('@app/redesign/features-v2/universe/create-universe/helpers/ToastUtils', () => ({
  useYBToast: () => ({ success: vi.fn(), error: vi.fn() })
}));

const universe = (federationConfigured: boolean) =>
  ({
    universeUUID: 'universe-1',
    universeDetails: {
      clusters: [{ clusterType: 'PRIMARY', userIntent: { federationConfigured } }]
    }
  }) as any;

const provider = (enableFederatedIam: boolean) => ({
  uuid: 'provider-1',
  code: 'aws',
  name: 'aws-prod',
  details: { cloudInfo: { aws: { enableFederatedIam } } }
});

const renderModal = (federationConfigured: boolean, providerEnabled: boolean) =>
  render(
    <ThemeProvider theme={mainTheme}>
      <CrossCloudFederationModal
        open
        onClose={vi.fn()}
        universeData={universe(federationConfigured)}
        provider={provider(providerEnabled)}
      />
    </ThemeProvider>
  );

const applyButton = () =>
  screen.getByTestId('CrossCloudFederationModal-Submit') as HTMLButtonElement;
const toggle = () => fireEvent.click(screen.getByTestId('CrossCloudFederationModal-Toggle'));

describe('CrossCloudFederationModal', () => {
  it('shows no banner and keeps Apply disabled until the setting changes', () => {
    renderModal(false, true);
    expect(screen.getByText('Edit Cross-Cloud Federated Access')).toBeTruthy();
    expect(screen.queryByText('Cloud provider configuration required')).toBeNull();
    expect(applyButton().disabled).toBe(true);
  });

  it('reminds to configure the provider when enabling with provider federation on', () => {
    renderModal(false, true);
    toggle();
    expect(screen.getByText('Cloud provider configuration required')).toBeTruthy();
    expect(screen.getByText('aws-prod')).toBeTruthy();
    expect(applyButton().disabled).toBe(false);
  });

  it('blocks enabling while the provider has federation off', () => {
    renderModal(false, false);
    toggle();
    expect(screen.getByText('Enable cloud service access to continue')).toBeTruthy();
    expect(screen.getByText('aws-prod').closest('a')).toBeTruthy();
    expect(applyButton().disabled).toBe(true);
  });

  it('warns about backups when disabling, whatever the provider setting', () => {
    renderModal(true, false);
    toggle();
    expect(
      screen.getByText('Disabling federated access will affect backups and restores')
    ).toBeTruthy();
    expect(applyButton().disabled).toBe(false);
  });
});
