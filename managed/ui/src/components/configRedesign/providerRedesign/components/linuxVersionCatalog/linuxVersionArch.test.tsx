import { FC, ReactNode } from 'react';
import { render } from '@testing-library/react';
import { FormProvider, useForm } from 'react-hook-form';
import { QueryClient, QueryClientProvider } from 'react-query';
import { ThemeProvider } from '@material-ui/core';
import { describe, expect, it, vi } from 'vitest';

import { mainTheme } from '../../../../../redesign/theme/mainTheme';
import {
  ImageBundle,
  ImageBundleType
} from '../../../../../redesign/features/universe/universe-form/utils/dto';
import { ArchitectureType, ProviderCode } from '../../constants';
import { AddLinuxVersionModal } from './AddLinuxVersionModal';
import { LinuxVersionsList } from './LinuxVersionsList';

vi.mock('../../../../../api/admin', () => ({
  fetchGlobalRunTimeConfigs: () => Promise.resolve({ data: { configEntries: [] } })
}));

const armBundle = {
  uuid: 'arm-bundle-uuid',
  name: 'custom-arm-bundle',
  useAsDefault: true,
  active: true,
  details: {
    arch: ArchitectureType.ARM64,
    globalYbImage: 'projects/p/global/images/alma-9-arm',
    regions: {},
    sshPort: 22,
    sshUser: 'centos'
  },
  metadata: { type: ImageBundleType.CUSTOM }
} as unknown as ImageBundle;

const FormWrapper: FC<{
  imageBundles?: ImageBundle[];
  children: (control: any) => ReactNode;
}> = ({ imageBundles = [], children }) => {
  const formMethods = useForm({ defaultValues: { imageBundles, regions: [] } });
  return <FormProvider {...formMethods}>{children(formMethods.control)}</FormProvider>;
};

const renderWithProviders = (ui: ReactNode) =>
  render(
    <QueryClientProvider
      client={new QueryClient({ defaultOptions: { queries: { retry: false } } })}
    >
      <ThemeProvider theme={mainTheme}>{ui}</ThemeProvider>
    </QueryClientProvider>
  );

// Providers with a single global image used to hide aarch64 entirely (PLAT-22425).
const GLOBAL_IMAGE_PROVIDERS = [ProviderCode.GCP, ProviderCode.AZU];

describe('Linux version catalog architecture support', () => {
  it.each(GLOBAL_IMAGE_PROVIDERS)('%s lists aarch64 image bundles', (providerType) => {
    const { getByText } = renderWithProviders(
      <FormWrapper imageBundles={[armBundle]}>
        {(control) => (
          <LinuxVersionsList
            control={control}
            providerType={providerType}
            viewMode="EDIT"
            inUseImageBundleUuids={new Set()}
          />
        )}
      </FormWrapper>
    );

    expect(getByText('custom-arm-bundle')).toBeInTheDocument();
  });

  it.each(GLOBAL_IMAGE_PROVIDERS)(
    '%s add Linux version modal offers the CPU architecture choice',
    (providerType) => {
      const { getByRole } = renderWithProviders(
        <FormWrapper>
          {(control) => (
            <AddLinuxVersionModal
              control={control}
              providerType={providerType}
              visible
              onHide={vi.fn()}
              onSubmit={vi.fn()}
            />
          )}
        </FormWrapper>
      );

      expect(getByRole('radio', { name: /aarch64/i })).toBeInTheDocument();
      expect(getByRole('radio', { name: /x86_64/i })).toBeChecked();
    }
  );
});
