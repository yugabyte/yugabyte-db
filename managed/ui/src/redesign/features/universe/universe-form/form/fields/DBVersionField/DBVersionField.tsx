import { ChangeEvent, ReactElement, useEffect } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext, useWatch } from 'react-hook-form';
import { Box, Tooltip } from '@material-ui/core';
import { YBLabel, YBAutoComplete } from '../../../../../../components';
import { api, DBReleasesQueryKey } from '../../../utils/api';
import { getActiveDBVersions, sortVersionStrings } from './DBVersionHelper';
import { isVersionStable } from '../../../../../../../utils/universeUtilsTyped';
import {
  CloudType,
  DEFAULT_ADVANCED_CONFIG,
  UniverseFormData,
  YBSoftwareMetadata
} from '../../../utils/dto';
import {
  isImgBundleSupportedByProvider,
  IsOsPatchingEnabled
} from '../../../../../../../components/configRedesign/providerRedesign/components/linuxVersionCatalog/LinuxVersionUtils';
import { ReleaseState } from '../../../../../releases/components/dtos';
import { ArchitectureType } from '../../../../../../../components/configRedesign/providerRedesign/constants';
import {
  SOFTWARE_VERSION_FIELD,
  PROVIDER_FIELD,
  CPU_ARCHITECTURE_FIELD
} from '../../../utils/constants';
import { useFormFieldStyles } from '../../../universeMainStyle';
import { isNonEmptyString } from '../../../../../../../utils/ObjectUtils';

import InfoMessageIcon from '../../../../../../../redesign/assets/info-message.svg';

const MAX_RELEASE_TAG_CHAR = 20;

interface DBVersionFieldProps {
  disabled?: boolean;
}

//Declarative methods
const getOptionLabel = (option: Record<string, string>): string => option.label ?? '';
const renderOption = (option: Record<string, string>) => {
  return (
    <Box
      style={{
        display: 'flex',
        flexDirection: 'row'
      }}
    >
      {option.label}
      {isNonEmptyString(option.releaseTag) && (
        <>
          <Box
            style={{
              border: '1px',
              borderRadius: '6px',
              padding: '3px 3px 3px 3px',
              backgroundColor: '#E9EEF2',
              maxWidth: 'fit-content',
              marginLeft: '4px',
              marginTop: '-4px'
            }}
          >
            <span
              data-testid={'DBVersionField-ReleaseTag'}
              style={{
                fontWeight: 400,
                fontFamily: 'Inter',
                fontSize: '11.5px',
                color: '#0B1117',
                alignSelf: 'center'
              }}
            >
              {option.releaseTag.length > MAX_RELEASE_TAG_CHAR
                ? `${option.releaseTag.substring(0, 10)}...`
                : option.releaseTag}
            </span>
          </Box>
          <span>
            {option.releaseTag.length > MAX_RELEASE_TAG_CHAR && (
              <Tooltip title={option.releaseTag} arrow placement="top">
                <img src={InfoMessageIcon} alt="info" />
              </Tooltip>
            )}
          </span>
        </>
      )}
    </Box>
  );
};

//Minimal fields
const transformData = (data: string[] | Record<string, YBSoftwareMetadata>) => {
  if (data && Array.isArray(data)) {
    return data.map((item) => ({
      label: item,
      value: item
    }));
  } else if (typeof data === 'object' && data !== null) {
    return getActiveDBVersions(data);
  } else {
    return [];
  }
};

export const DBVersionField = ({ disabled }: DBVersionFieldProps): ReactElement => {
  const { control, setValue, getValues } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();
  const classes = useFormFieldStyles();

  //watchers
  const provider = useWatch({ name: PROVIDER_FIELD });
  const cpuArch = useWatch({ name: CPU_ARCHITECTURE_FIELD });
  const isOsPatchingEnabled = IsOsPatchingEnabled();

  const { data, isLoading } = useQuery(
    [DBReleasesQueryKey.provider(provider?.uuid), isOsPatchingEnabled ? cpuArch : null],
    () =>
      api.getDBVersions(
        true,
        /* Pass x86_64 as default version to search in case of AWS when OS patching is enabled
          Checking this condition only for AWS as we support OS patching only for AWS based on
          line 56 from CPUArchField.tsx
        */
        isOsPatchingEnabled &&
          isImgBundleSupportedByProvider(provider) &&
          provider?.code === CloudType.aws
          ? cpuArch ?? ArchitectureType.X86_64
          : null,
        true
      ),
    {
      enabled: !!provider?.uuid,
      onSuccess: (data) => {
        //pre-select first available db version
        const stableSorted: Record<string, string>[] = sortVersionStrings(
          data?.filter((versionData: any) => {
            return isVersionStable(versionData.label.version);
          }),
          true
        );
        // Display the latest stable version on the Create Universe page
        if (!getValues(SOFTWARE_VERSION_FIELD) && stableSorted.length) {
          setValue(SOFTWARE_VERSION_FIELD, stableSorted[0].value, { shouldValidate: true });
        }
      },
      select: transformData
    }
  );

  useEffect(() => {
    if (isOsPatchingEnabled && !disabled) {
      setValue(SOFTWARE_VERSION_FIELD, null);
    }
  }, [cpuArch, isOsPatchingEnabled]);

  const handleChange = (e: ChangeEvent<{}>, option: any) => {
    setValue(SOFTWARE_VERSION_FIELD, option?.value ?? DEFAULT_ADVANCED_CONFIG.ybSoftwareVersion, {
      shouldValidate: true
    });
  };

  const stableDbVersions: Record<string, string>[] = data
    ? sortVersionStrings(
        data?.filter((versionData: any) => {
          return (
            versionData.label.state === ReleaseState.ACTIVE &&
            isVersionStable(versionData.label.version)
          );
        }),
        true
      )
    : [];

  const previewDbVersions: Record<string, string>[] = data
    ? sortVersionStrings(
        data?.filter((versionData: any) => {
          return (
            versionData.label.state === ReleaseState.ACTIVE &&
            !isVersionStable(versionData.label.version)
          );
        }),
        true
      )
    : [];

  // Display the Stable versions first, followed by the Preview versions
  const dbVersions: Record<string, any>[] = [
    ...stableDbVersions.map((stableDbVersion: Record<string, string>) => ({
      label: stableDbVersion.value,
      releaseTag: stableDbVersion.releaseTag,
      value: stableDbVersion.value,
      series: `v${stableDbVersion.value.split('.')[0]}.${
        stableDbVersion.value.split('.')[1]
      } Series (Stable)`
    })),
    ...previewDbVersions.map((previewDbVersion: Record<string, string>) => ({
      label: previewDbVersion.value,
      releaseTag: previewDbVersion.releaseTag,
      value: previewDbVersion.value,
      series: `v${previewDbVersion.value.split('.')[0]}.${
        previewDbVersion.value.split('.')[1]
      } Series (Preview)`
    }))
  ];

  return (
    <Controller
      name={SOFTWARE_VERSION_FIELD}
      control={control}
      rules={{
        required: !disabled
          ? (t('universeForm.validation.required', {
              field: t('universeForm.advancedConfig.dbVersion')
            }) as string)
          : ''
      }}
      render={({ field, fieldState }) => {
        const value = dbVersions.find((version) => version.value === field.value) ?? '';
        return (
          <Box display="flex" width="100%" data-testid="DBVersionField-Container">
            <YBLabel dataTestId="DBVersionField-Label" className={classes.advancedConfigLabel}>
              {t('universeForm.advancedConfig.dbVersion')}
            </YBLabel>
            <Box flex={1} className={classes.defaultTextBox}>
              <YBAutoComplete
                disabled={disabled}
                loading={isLoading}
                options={(dbVersions as unknown) as Record<string, any>[]}
                groupBy={(option: Record<string, string>) => option.series}
                getOptionLabel={getOptionLabel}
                renderOption={renderOption}
                onChange={handleChange}
                value={(value as unknown) as never}
                ybInputProps={{
                  error: !!fieldState.error,
                  helperText: fieldState.error?.message,
                  'data-testid': 'DBVersionField-AutoComplete'
                }}
              />
            </Box>
          </Box>
        );
      }}
    />
  );
};
