import { makeStyles, MenuItem, Typography } from '@material-ui/core';
import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { FieldValues, UseControllerProps } from 'react-hook-form';

import { api, telemetryProviderQueryKey } from '../../../helpers/api';
import { RedirectToTelemetryConfigurationModal } from './RedirectToTelementryConfigurationModal';
import { YBSelectField } from '@app/redesign/components';
import { TelemetryProvider } from '../dtos';
import { TelemetryProviderType } from '../types';
import { TP_FRIENDLY_NAMES } from '../constants';
import AddIcon from '../../../assets/add.svg';

import { usePillStyles } from '@app/redesign/styles/styles';

export interface TelemetryProviderConfigSelectOption {
  value: string;
  label: string;
  telemetryProviderType: TelemetryProviderType;
}
export interface TelemetryProviderSelectFieldProps<TFieldValues extends FieldValues> {
  useControllerProps: UseControllerProps<TFieldValues>;

  isDisabled?: boolean;
  telemetryProviderFilter?: (options: TelemetryProvider) => boolean;
}

const useStyles = makeStyles((theme) => ({
  optionListContainer: {
    maxHeight: 200,
    overflowY: 'auto'
  },
  optionContainer: {
    display: 'flex',
    alignItems: 'center'
  },
  optionPillContainer: {
    display: 'flex',
    gap: theme.spacing(1),

    marginLeft: theme.spacing(1)
  },
  configMenuList: {
    paddingBottom: 0
  },
  configSelectCreateNewAction: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    height: 'fit-content',
    padding: `${theme.spacing(1)}px ${theme.spacing(2)}px`,
    position: 'sticky',
    bottom: 0,

    color: theme.palette.primary[600],
    backgroundColor: theme.palette.common.white,
    borderTop: `1px solid ${theme.palette.grey[200]}`,
    cursor: 'pointer'
  }
}));

export interface StorageConfigOption {
  label: string;
  value: {
    name: string;
    uuid: string;
  };
}

const TRANSLATION_KEY_PREFIX = 'telemetryProviderConfigSelect';
const COMPONENT_NAME = 'TelemetryProviderConfigSelect';
/**
 * Handles option loading internally.
 * Compatible with react-hook-form.
 */
export const TelemetryProviderConfigSelectField = <TFieldValues extends FieldValues>({
  useControllerProps,
  isDisabled = false,
  telemetryProviderFilter
}: TelemetryProviderSelectFieldProps<TFieldValues>) => {
  const [isRedirectConfirmationModalOpen, setIsRedirectConfirmationModalOpen] = useState(false);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const pillClasses = usePillStyles();

  const telemetryProvidersQuery = useQuery(
    telemetryProviderQueryKey.list(),
    () => api.fetchTelemetryProviderList(),
    {
      select: (telemetryProviderList) =>
        telemetryProviderList.reduce((filteredTelemetryProviderList, telemetryProvider) => {
          if (!telemetryProviderFilter || telemetryProviderFilter(telemetryProvider)) {
            filteredTelemetryProviderList.push({
              value: telemetryProvider.uuid,
              label: telemetryProvider.name,
              telemetryProviderType: telemetryProvider.config.type
            });
          }
          return filteredTelemetryProviderList;
        }, [] as TelemetryProviderConfigSelectOption[])
    }
  );

  const handleCreateTelemetryConfigurationClick = (event: React.MouseEvent) => {
    event.preventDefault();
    event.stopPropagation();
    setIsRedirectConfirmationModalOpen(true);
  };
  const closeRedirectConfirmationModal = () => setIsRedirectConfirmationModalOpen(false);

  return (
    <div>
      <YBSelectField
        fullWidth
        control={useControllerProps.control}
        name={useControllerProps.name}
        inputProps={{
          'data-testid': `${COMPONENT_NAME}-SelectInput`
        }}
        SelectProps={{
          MenuProps: {
            MenuListProps: {
              style: {
                paddingBottom: 0
              }
            }
          }
        }}
        disabled={isDisabled}
      >
        {telemetryProvidersQuery.data?.map((telemetryProviderOption) => (
          <MenuItem
            key={telemetryProviderOption.value}
            value={telemetryProviderOption.value}
            className={classes.optionContainer}
          >
            <Typography variant="body2">{telemetryProviderOption.label}</Typography>
            <div className={classes.optionPillContainer}>
              <div className={pillClasses.pill}>
                {TP_FRIENDLY_NAMES[telemetryProviderOption.telemetryProviderType]}
              </div>
            </div>
          </MenuItem>
        ))}
        <MenuItem
          data-testid={`${COMPONENT_NAME}-CreateNewOption`}
          value="__createNew__"
          className={classes.configSelectCreateNewAction}
          onClickCapture={handleCreateTelemetryConfigurationClick}
        >
          <AddIcon />
          <Typography variant="body2">{t('createTelemetryConfiguration')}</Typography>
        </MenuItem>
      </YBSelectField>
      <RedirectToTelemetryConfigurationModal
        open={isRedirectConfirmationModalOpen}
        onClose={closeRedirectConfirmationModal}
      />
    </div>
  );
};
