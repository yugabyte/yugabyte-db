import { useFormContext } from 'react-hook-form';

import { YBDropZoneField } from './YBDropZone/YBDropZoneField';
import { ProviderCode } from '../constants';
import { getIsFieldDisabled } from '../forms/utils';
import { FormField } from '../forms/components/FormField';
import { FieldLabel } from '../forms/components/FieldLabel';

interface OciApiPrivateKeyFieldProps {
  isFormDisabled: boolean;
  isProviderInUse?: boolean;
}

export const OciApiPrivateKeyField = ({
  isFormDisabled,
  isProviderInUse = false
}: OciApiPrivateKeyFieldProps) => {
  const { control } = useFormContext();

  return (
    <FormField>
      <FieldLabel>API Private Key</FieldLabel>
      <YBDropZoneField
        name="ociPrivateKeyContent"
        control={control}
        actionButtonText="Upload API Private Key PEM File"
        multipleFiles={false}
        showHelpText={false}
        disabled={getIsFieldDisabled(
          ProviderCode.OCI,
          'ociPrivateKeyContent',
          isFormDisabled,
          isProviderInUse
        )}
      />
    </FormField>
  );
};
