import { Box } from '@material-ui/core';
import { useFormContext } from 'react-hook-form';

import {
  OptionProps,
  RadioGroupOrientation,
  YBInputField,
  YBRadioGroupField
} from '../../../../redesign/components';
import { YBDropZoneField } from './YBDropZone/YBDropZoneField';
import { ProviderCode, SshPrivateKeyInputType } from '../constants';
import { getIsFieldDisabled } from '../forms/utils';
import { FormField } from '../forms/components/FormField';
import { FieldLabel } from '../forms/components/FieldLabel';

interface SshPrivateKeyFormFieldProps {
  isFormDisabled: boolean;
  providerCode: ProviderCode;

  isProviderInUse?: boolean;
}

export const SshPrivateKeyFormField = ({
  isFormDisabled,
  providerCode,
  isProviderInUse = false
}: SshPrivateKeyFormFieldProps) => {
  const {
    control,
    watch,
    formState: { defaultValues }
  } = useFormContext();

  const sshPrivateKeyInputTypeOptions: OptionProps[] = [
    { value: SshPrivateKeyInputType.UPLOAD_KEY, label: 'Upload Key' },
    { value: SshPrivateKeyInputType.PASTE_KEY, label: 'Paste Key' }
  ];

  const sshPrivateKeyInputType = watch('sshPrivateKeyInputType');
  return (
    <>
      <FormField>
        <FieldLabel>SSH Private Key Content</FieldLabel>
        <YBRadioGroupField
          name="sshPrivateKeyInputType"
          control={control}
          options={sshPrivateKeyInputTypeOptions}
          orientation={RadioGroupOrientation.HORIZONTAL}
          isDisabled={isFormDisabled}
        />
      </FormField>
      <FormField>
        <FieldLabel />
        {sshPrivateKeyInputType === SshPrivateKeyInputType.UPLOAD_KEY ? (
          <YBDropZoneField
            name="sshPrivateKeyContent"
            control={control}
            actionButtonText="Upload SSH Key PEM File"
            multipleFiles={false}
            showHelpText={false}
            disabled={getIsFieldDisabled(
              providerCode,
              'sshPrivateKeyContent',
              isFormDisabled,
              isProviderInUse
            )}
          />
        ) : (
          <YBInputField
            control={control}
            name="sshPrivateKeyContentText"
            multiline
            fullWidth
            minRows={8}
            maxRows={8}
            placeholder="Paste key here..."
            disabled={getIsFieldDisabled(
              providerCode,
              'sshPrivateKeyContentText',
              isFormDisabled,
              isProviderInUse
            )}
          />
        )}
      </FormField>
    </>
  );
};
