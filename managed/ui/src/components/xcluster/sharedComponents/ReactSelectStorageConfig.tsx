import { ReactNode } from 'react';
import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { components, OptionProps, SingleValueProps, Styles } from 'react-select';
import { FieldValues } from 'react-hook-form';
import { useSelector } from 'react-redux';
import { groupBy } from 'lodash';
import { Trans, useTranslation } from 'react-i18next';
import { Link } from 'react-router';

import {
  ReactSelectGroupedOption,
  YBReactSelectField,
  YBReactSelectFieldProps
} from '../../configRedesign/providerRedesign/components/YBReactSelect/YBReactSelectField';
import { usePillStyles } from '../../../redesign/styles/styles';
import { IStorageConfig as BackupStorageConfig } from '../../backupv2';

import { Optional } from '../../../redesign/helpers/types';

const useSelectStyles = makeStyles((theme) => ({
  optionLabel: {
    display: 'flex',
    alignItems: 'center'
  },
  optionPillContainer: {
    display: 'flex',
    gap: theme.spacing(1),

    marginLeft: 'auto'
  }
}));

export interface StorageConfigOption {
  label: string;
  value: {
    name: string;
    regions: any[];
    uuid: string;
  };
}

const TRANSLATION_KEY_PREFIX = 'storageConfig';

/**
 * Wrapper component around `YBReactSelectField`
 * - Adds storage config specific styling and customization.
 * - Default storage config options are provided and can be overriden.
 */
export const ReactSelectStorageConfigField = <TFieldValues extends FieldValues>(
  props: Optional<YBReactSelectFieldProps<TFieldValues>, 'options'>
) => {
  const storageConfigs: BackupStorageConfig[] = useSelector((reduxState: any) =>
    reduxState?.customer?.configs?.data.filter(
      (storageConfig: BackupStorageConfig) => storageConfig.type === 'STORAGE'
    )
  );
  const theme = useTheme();
  /**
   * Style overrides for components provided by React-Select.
   */
  const storageConfigSelectStylesOverride: Partial<Styles> = {
    singleValue: (baseStyles) => ({
      ...baseStyles,
      display: 'flex',

      width: '100%',
      paddingRight: theme.spacing(1)
    }),
    option: (baseStyles) => ({
      ...baseStyles,
      display: 'flex'
    })
  };

  const storageConfigsOptions = storageConfigs.map((storageConfig) => {
    return {
      value: {
        uuid: storageConfig.configUUID,
        name: storageConfig.name,
        regions: storageConfig.data?.REGION_LOCATIONS
      },
      label: storageConfig.configName
    };
  });
  const groupedStorageConfigOptions: ReactSelectGroupedOption[] = Object.entries(
    groupBy(storageConfigsOptions, (configOption) => configOption.value.name)
  ).map(([label, options]) => ({ label, options }));
  return (
    <Box display="flex" flexDirection="column" gridGap={theme.spacing(1)}>
      <YBReactSelectField
        options={groupedStorageConfigOptions}
        stylesOverride={storageConfigSelectStylesOverride}
        components={{
          SingleValue: SingleValue,
          Option: Option
        }}
        {...props}
      />
      {groupedStorageConfigOptions.length <= 0 && (
        <Typography variant="body2">
          <Trans
            i18nKey={`${TRANSLATION_KEY_PREFIX}.createBackupStorageConfigPrompt`}
            components={{ createStorageConfigLink: <Link to={'/config/backup'} /> }}
          />
        </Typography>
      )}
    </Box>
  );
};

/**
 * Shared inner elements for laying out a React-Select storage config option.
 */
const ReactSelectStorageConfigOption = ({
  children,
  option
}: {
  children: ReactNode;
  option: StorageConfigOption;
}) => {
  const selectClasses = useSelectStyles();
  const pillClasses = usePillStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  return (
    <>
      <span className={selectClasses.optionLabel}>{children}</span>
      <div className={selectClasses.optionPillContainer}>
        <div className={pillClasses.pill}>{option.value.name}</div>
        <div className={pillClasses.pill}>{t('pill.multiRegionSupport')}</div>
      </div>
    </>
  );
};

/**
 * Customized React-Select SingleValue component for storage config options.
 */
export const SingleValue = ({ children, ...props }: SingleValueProps<StorageConfigOption>) => (
  <components.SingleValue {...props}>
    <ReactSelectStorageConfigOption option={props.data}>{children}</ReactSelectStorageConfigOption>
  </components.SingleValue>
);
/**
 * Customized React-Select Option component for storage config options.
 */
export const Option = ({ children, ...props }: OptionProps<StorageConfigOption>) => (
  <components.Option {...props}>
    <ReactSelectStorageConfigOption option={props.data}>{children}</ReactSelectStorageConfigOption>
  </components.Option>
);
