import { FC, ReactNode } from 'react';
import { render } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { FormProvider, useForm } from 'react-hook-form';
import { QueryClient, QueryClientProvider } from 'react-query';
import { ThemeProvider } from '@material-ui/core';
import { describe, expect, it, vi } from 'vitest';

import { mainTheme } from '../../../../../redesign/theme/mainTheme';
import {
  ImageBundle,
  ImageBundleType
} from '../../../../../redesign/features/universe/universe-form/utils/dto';
import { ArchitectureType, ProviderCode, ProviderOperation } from '../../constants';
import { AddLinuxVersionModal } from './AddLinuxVersionModal';
import { LinuxVersionCatalog } from './LinuxVersionCatalog';
import { LinuxVersionsList } from './LinuxVersionsList';

// LinuxVersionCatalog renders nothing unless VM OS patching (VM_PATCHING_RUNTIME_CONFIG) is
// enabled. vi.mock is hoisted above imports, so the key is spelled out.
vi.mock('../../../../../api/admin', () => ({
  fetchGlobalRunTimeConfigs: () =>
    Promise.resolve({
      data: { configEntries: [{ key: 'yb.provider.vm_os_patching', value: 'true' }] }
    })
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
  const formMethods = useForm({
    defaultValues: { imageBundles, regions: [], dbNodePublicInternetAccess: true }
  });
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

describe('Linux version catalog YugabyteDB version checkboxes', () => {
  const renderCatalog = (providerType: ProviderCode) =>
    renderWithProviders(
      <FormWrapper>
        {(control) => (
          <LinuxVersionCatalog
            control={control}
            providerType={providerType}
            providerOperation={ProviderOperation.CREATE}
            isDisabled={false}
          />
        )}
      </FormWrapper>
    );

  it('GCP: unticking x86_64 unticks "include YugabyteDB versions"', async () => {
    const { findByTestId, getByTestId, queryByTestId } = renderCatalog(ProviderCode.GCP);
    const include = await findByTestId('LinuxVersionCatalog-IncludeYBVersion');

    await userEvent.click(include);
    expect(include).toBeChecked();
    expect(queryByTestId('LinuxVersionCatalog-IncludeAarch')).not.toBeInTheDocument();

    await userEvent.click(getByTestId('LinuxVersionCatalog-Includex86'));
    expect(include).not.toBeChecked();
  });

  it('AWS: "include YugabyteDB versions" stays ticked until both architectures are unticked', async () => {
    const { findByTestId, getByTestId } = renderCatalog(ProviderCode.AWS);
    const include = await findByTestId('LinuxVersionCatalog-IncludeYBVersion');

    await userEvent.click(include);
    await userEvent.click(getByTestId('LinuxVersionCatalog-Includex86'));
    expect(include).toBeChecked();

    await userEvent.click(getByTestId('LinuxVersionCatalog-IncludeAarch'));
    expect(include).not.toBeChecked();
  });
});
