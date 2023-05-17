import React, { ChangeEvent, FC } from 'react';
import { useQuery } from 'react-query';
import { Controller, useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { Chip } from '@material-ui/core';
import { YBAutoComplete } from '../../../../../components';
import { EncryptionAtRestFormValues, KMS_FIELD_NAME } from '../EncryptionAtRestUtils';
import { useMKRStyles } from '../EncryptionAtRestUtils';
import { api, QUERY_KEY } from '../../../../../utils/api';
import { KmsConfig } from '../../../universe-form/utils/dto';

//KMS Field
interface KMSFieldProps {
  disabled: boolean;
  label: string;
  activeKMS?: string;
}

export const KMSField: FC<KMSFieldProps> = ({ disabled, label, activeKMS }) => {
  const { t } = useTranslation();
  const { control, setValue } = useFormContext<EncryptionAtRestFormValues>();
  const classes = useMKRStyles();
  const { data: kmsConfigs = [], isLoading } = useQuery(QUERY_KEY.getKMSConfigs, api.getKMSConfigs);

  const handleChange = (e: ChangeEvent<{}>, option: any) => {
    setValue(KMS_FIELD_NAME, option?.metadata?.configUUID ?? null, {
      shouldValidate: true
    });
  };

  const renderOption = (op: Record<string, string>): React.ReactElement => {
    const option = (op as unknown) as KmsConfig;
    const isActive = activeKMS && activeKMS === option?.metadata?.configUUID;
    return (
      <>
        {' '}
        {option.metadata.name}
        {isActive && <Chip size="small" className={classes.chip} label={t('common.active')} />}
      </>
    );
  };

  const renderOptionLabel = (op: Record<string, string>): string => {
    const option = (op as unknown) as KmsConfig;
    return option?.metadata?.name ?? '';
  };

  return (
    <Controller
      name={KMS_FIELD_NAME}
      control={control}
      rules={{
        required: true
      }}
      render={({ field }) => {
        const value = kmsConfigs.find((i) => i.metadata.configUUID === field.value) ?? '';
        return (
          <YBAutoComplete
            disabled={disabled}
            loading={isLoading}
            options={(kmsConfigs as unknown) as Record<string, string>[]}
            ybInputProps={{
              placeholder: t('common.select'),
              InputProps: { autoFocus: true },
              'data-testid': 'KMSField-AutoComplete',
              label
            }}
            ref={field.ref}
            getOptionLabel={renderOptionLabel}
            renderOption={renderOption}
            onChange={handleChange}
            value={(value as unknown) as never}
            classes={{
              root: classes.kmsField
            }}
          />
        );
      }}
    />
  );
};
