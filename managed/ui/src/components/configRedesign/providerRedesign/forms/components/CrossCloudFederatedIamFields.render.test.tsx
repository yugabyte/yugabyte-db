import { ReactNode } from 'react';
import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { FormProvider, useForm } from 'react-hook-form';
import { QueryClient, QueryClientProvider } from 'react-query';
import { ThemeProvider } from '@material-ui/core';
import { YBThemeProvider, coreTheme } from '@yugabyte-ui-library/core';
import { yupResolver } from '@hookform/resolvers/yup';
import { object } from 'yup';

import { mainTheme } from '../../../../../redesign/theme/mainTheme';
import {
  CloudAccessHeading,
  CrossCloudFederatedIamFields,
  CrossCloudFederatedIamFormValues,
  FEDERATED_IAM_VALIDATION
} from './CrossCloudFederatedIamFields';

vi.mock('../../../../../redesign/helpers/api', async (importOriginal) => ({
  ...(await importOriginal<any>()),
  api: { fetchRuntimeConfigs: () => Promise.resolve({}) }
}));
const preview = vi.hoisted(() => ({ enabled: true }));
vi.mock('../../../../backupv2/common/BackupUtils', () => ({
  isCrossCloudFederatedIamEnabled: () => preview.enabled
}));

const AUDIENCE =
  '//iam.googleapis.com/projects/1/locations/global/workloadIdentityPools/p/providers/a';

let latestValues: () => CrossCloudFederatedIamFormValues;

const Harness = ({
  providerCloud,
  defaultValues
}: {
  providerCloud?: 'aws' | 'gcp';
  defaultValues: CrossCloudFederatedIamFormValues;
}) => {
  const formMethods = useForm<CrossCloudFederatedIamFormValues>({
    defaultValues,
    resolver: yupResolver(object().shape(FEDERATED_IAM_VALIDATION))
  });
  latestValues = formMethods.getValues;
  return (
    <FormProvider {...formMethods}>
      <CrossCloudFederatedIamFields
        control={formMethods.control}
        isFormDisabled={false}
        providerCloud={providerCloud}
      />
    </FormProvider>
  );
};

const renderWithProviders = (ui: ReactNode) =>
  render(
    <QueryClientProvider client={new QueryClient()}>
      <YBThemeProvider theme={coreTheme}>
        <ThemeProvider theme={mainTheme}>{ui}</ThemeProvider>
      </YBThemeProvider>
    </QueryClientProvider>
  );

describe('CrossCloudFederatedIamFields', () => {
  afterEach(() => {
    preview.enabled = true;
  });

  it('renders no federated IAM controls while the preview flag is off', async () => {
    preview.enabled = false;
    renderWithProviders(
      <>
        <CloudAccessHeading infoTitle="Access to AWS" infoContent="help">
          Access to AWS
        </CloudAccessHeading>
        <Harness defaultValues={{}} />
        <Harness providerCloud="aws" defaultValues={{ federationGcsEnabled: true }} />
      </>
    );
    await waitFor(() => expect(screen.queryByText('Access to AWS')).toBeNull());
    expect(screen.queryByText('Cross Cloud Federated Access')).toBeNull();
    expect(screen.queryByText('Access to other clouds')).toBeNull();
    expect(screen.queryByText('Google Cloud Storage')).toBeNull();
  });

  it('shows only the other cloud for an AWS provider, and its fields once turned on', async () => {
    renderWithProviders(
      <Harness providerCloud="aws" defaultValues={{ federationGcsEnabled: false }} />
    );
    expect(await screen.findByText('Access to other clouds')).toBeTruthy();
    expect(screen.queryByText('Amazon S3')).toBeNull();
    expect(screen.queryByText('Credential configuration file')).toBeNull();

    fireEvent.click(screen.getByRole('checkbox'));
    expect(await screen.findByText('Credential configuration file')).toBeTruthy();
  });

  it('shows both clouds in their own panel for on-prem', async () => {
    renderWithProviders(<Harness defaultValues={{}} />);
    expect(await screen.findByText('Cross Cloud Federated Access')).toBeTruthy();
    expect(screen.getByText('Amazon S3')).toBeTruthy();
    expect(screen.getByText('Google Cloud Storage')).toBeTruthy();
  });

  it('links each turned-on card to the setup guide', async () => {
    renderWithProviders(
      <Harness defaultValues={{ federationS3Enabled: true, federationGcsEnabled: true }} />
    );
    const links = await screen.findAllByText('view the setup guide');
    expect(links).toHaveLength(2);
    links.forEach((link) => expect(link.closest('a')?.getAttribute('href')).toBe('#'));
  });

  it('shows the saved GCS audience and replaces it from an uploaded file', async () => {
    renderWithProviders(
      <Harness
        providerCloud="aws"
        defaultValues={{ federationGcsEnabled: true, federationGcsAudience: 'old-audience' }}
      />
    );
    expect(await screen.findByText('Configured audience: old-audience')).toBeTruthy();

    const file = new File(
      [JSON.stringify({ type: 'external_account', audience: AUDIENCE })],
      'creds.json',
      { type: 'application/json' }
    );
    const input = document.querySelector('input[type="file"]') as HTMLInputElement;
    fireEvent.change(input, { target: { files: [file] } });

    await waitFor(() => expect(latestValues().federationGcsAudience).toBe(AUDIENCE));
    expect(screen.queryByText(/Configured audience/)).toBeNull();
    expect(screen.getByText('creds.json')).toBeTruthy();
  });

  it('rejects an uploaded file that impersonates a service account', async () => {
    renderWithProviders(
      <Harness providerCloud="aws" defaultValues={{ federationGcsEnabled: true }} />
    );
    const file = new File(
      [
        JSON.stringify({
          type: 'external_account',
          audience: AUDIENCE,
          service_account_impersonation_url: 'https://iamcredentials.googleapis.com/x'
        })
      ],
      'creds.json',
      { type: 'application/json' }
    );
    const input = (await waitFor(() =>
      document.querySelector('input[type="file"]')
    )) as HTMLInputElement;
    fireEvent.change(input, { target: { files: [file] } });

    expect(await screen.findByText(/impersonation is not supported/)).toBeTruthy();
    expect(latestValues().federationGcsAudience).toBeFalsy();
  });
});
