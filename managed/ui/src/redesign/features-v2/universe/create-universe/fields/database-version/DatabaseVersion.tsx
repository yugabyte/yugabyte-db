/*
 * Created on Wed Apr 02 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { ChangeEvent } from 'react';
import { useQuery } from 'react-query';
import {
  Controller,
  FieldValues,
  Path,
  useFormContext
} from 'react-hook-form';
import { mui, YBAutoComplete, YBLabel, YBSelectProps, YBTooltip } from '@yugabyte-ui-library/core';
import { isNonEmptyString } from '../../../../../../utils/ObjectUtils';
import { MAX_RELEASE_TAG_CHAR } from '../../../../../features/releases/helpers/utils';
import { sortVersionStrings } from '../../../../../features/universe/universe-form/form/fields/DBVersionField/DBVersionHelper';
import { ReleaseState } from '../../../../../features/releases/components/dtos';
import { DEFAULT_ADVANCED_CONFIG } from '../../../../../features/universe/universe-form/utils/dto';
import { isVersionStable } from '../../../../../../utils/universeUtilsTyped';
import {
  CREATE_UNIVERSE_DB_VERSIONS_QUERY_KEY,
  fetchCreateUniverseDbVersions,
  transformDbVersionQueryData
} from '../../helpers/generalSettingsDefaults';

//icons
import InfoMessageIcon from '../../../../../assets/info-message.svg';

const { Box } = mui;

interface DatabaseVersionFieldProps<T extends FieldValues>
  extends Omit<YBSelectProps, 'name' | 'control'> {
  name: Path<T>;
  label: string;
  placeholder?: string;
  disabled?: boolean;
}
const getOptionLabel = (option: string | Record<string, string>): string =>
  typeof option === 'string' ? option : option.label ?? '';
const renderOption = (
  props: React.HTMLAttributes<HTMLLIElement>,
  option: Record<string, string>
) => {
  return (
    <li {...props}>
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
                <YBTooltip title={option.releaseTag} arrow placement="top">
                  <img src={InfoMessageIcon} alt="info" />
                </YBTooltip>
              )}
            </span>
          </>
        )}
      </Box>
    </li>
  );
};

export const DatabaseVersionField = <T extends FieldValues>({
  name,
  label,
  placeholder,
  sx,
  disabled
}: DatabaseVersionFieldProps<T>) => {
  const { control, setValue } = useFormContext<T>();

  const { data, isLoading } = useQuery(
    CREATE_UNIVERSE_DB_VERSIONS_QUERY_KEY,
    fetchCreateUniverseDbVersions,
    {
      select: transformDbVersionQueryData
    }
  );

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

  const handleChange = (_e: ChangeEvent<{}>, option: any) => {
    setValue(name, option?.value ?? DEFAULT_ADVANCED_CONFIG.ybSoftwareVersion, {
      shouldValidate: true
    });
  };

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
      name={name}
      control={control}
      render={({ field, fieldState }) => {
        const value = dbVersions.find((version) => version.value === field.value) ?? '';
        return (
          <div>
            <YBLabel error={!!fieldState.error}>{label}</YBLabel>
            <Box flex={1}>
              <YBAutoComplete
                loading={isLoading}
                options={(dbVersions as unknown) as Record<string, any>[]}
                groupBy={(option: Record<string, string>) => option.series}
                getOptionLabel={getOptionLabel}
                renderOption={renderOption}
                onChange={handleChange}
                value={(value as unknown) as never}
                dataTestId="database-version-field-container"
                ybInputProps={{
                  error: !!fieldState.error,
                  helperText: fieldState.error?.message,
                  placeholder: placeholder,
                  dataTestId: 'database-version-field'
                }}
                sx={sx}
                size="large"
                disabled={disabled}
              />
            </Box>
          </div>
        );
      }}
    />
  );
};
