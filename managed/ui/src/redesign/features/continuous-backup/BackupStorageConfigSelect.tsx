import { makeStyles, Typography } from '@material-ui/core';
import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import Select, { components, MenuListComponentProps, Props as SelectProps } from 'react-select';

import { CustomerConfig } from '../../../components/backupv2';
import { YBErrorIndicator, YBLoading } from '../../../components/common/indicators';
import { YBButton } from '../../components';
import { api, CUSTOMER_CONFIG_QUERY_KEY } from '../../helpers/api';
import { ApiPermissionMap } from '../rbac/ApiAndUserPermMapping';
import { RbacValidator } from '../rbac/common/RbacApiPermValidator';
import { RedirectToStorageConfigConfigurationModal } from './RedirectToStorageConfigConfigurationModal';
import { getStorageConfigs } from './utils';
import AddIcon from '../../assets/add.svg';

export interface BackupStorageConfigReactSelectOption {
  value: string;
  label: string;

  isDisabled?: boolean;
}
export interface BackupStorageConfigSelectProps
  extends SelectProps<BackupStorageConfigReactSelectOption> {
  onOptionsLoaded?: (options: BackupStorageConfigReactSelectOption[]) => void;
}

const useStyles = makeStyles((theme) => ({
  configureBackupStorageConfigPrompt: {
    display: 'flex',
    alignItems: 'center',
    flexDirection: 'column',
    gap: theme.spacing(2),

    padding: theme.spacing(4),

    borderRadius: '8px',
    border: `1px solid ${theme.palette.grey[200]}`,
    background: theme.palette.common.white,
    boxShadow: '0px 1px 4px 0px rgba(153, 153, 153, 0.50)'
  },
  promptPrimaryText: {
    fontSize: '11.5px',
    fontWeight: 400,
    color: theme.palette.ybacolors.textDarkGray
  },
  promptPrimaryActionButton: {
    height: '30px',
    padding: `${theme.spacing(0.5)}px ${theme.spacing(1)}px`,

    color: theme.palette.ybacolors.ybDarkGray,
    fontSize: '12px',
    fontWeight: 500,
    border: `1px solid ${theme.palette.ybacolors.borderGray}`,
    borderRadius: '6px'
  },
  storageConfigMenuList: {
    paddingBottom: 0
  },
  storageConfigSelectCreateNewAction: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    padding: `${theme.spacing(1)}px ${theme.spacing(2)}px`,

    color: theme.palette.primary[600],
    borderTop: `1px solid ${theme.palette.grey[200]}`,
    cursor: 'pointer'
  }
}));

const TRANSLATION_KEY_PREFIX = 'component.backupStorageConfigSelect';
const COMPONENT_NAME = 'BackupStorageConfigSelect';

/**
 * Handles fetching storage config options and provides option to create
 * a new storage config.
 */
export const BackupStorageConfigSelect = ({
  onOptionsLoaded,
  ...reactSelectProps
}: BackupStorageConfigSelectProps) => {
  const [isRedirectConfirmationModalOpen, setIsRedirectConfirmationModalOpen] = useState(false);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();

  const customerConfigsQuery = useQuery(CUSTOMER_CONFIG_QUERY_KEY, () => api.getCustomerConfig(), {
    select: getStorageConfigOptions,
    onSuccess: (storageConfigsOptions) => {
      onOptionsLoaded?.(storageConfigsOptions);
    }
  });

  const openRedirectConfirmationModal = () => setIsRedirectConfirmationModalOpen(true);
  const closeRedirectConfirmationModal = () => setIsRedirectConfirmationModalOpen(false);

  if (customerConfigsQuery.isLoading || customerConfigsQuery.isIdle) {
    return <YBLoading />;
  }
  if (customerConfigsQuery.isError) {
    return <YBErrorIndicator customerErrorMessage={t('error.failedToFetchStorageConfigs')} />;
  }

  const getNoMatchingOptionsMessage = (_: { inputValue: string }) =>
    t('selectComponent.noMatchingOptions');
  const storageConfigOptions = customerConfigsQuery.data;
  return (
    <div>
      {storageConfigOptions.length > 0 ? (
        <div>
          <Select
            options={storageConfigOptions}
            noOptionsMessage={getNoMatchingOptionsMessage}
            captureMenuScroll={false}
            {...reactSelectProps}
            components={{
              MenuList: (menuListProps) => (
                <StorageConfigMenuList
                  {...menuListProps}
                  openRedirectConfirmationModal={openRedirectConfirmationModal}
                />
              ),
              ...reactSelectProps.components
            }}
          />
        </div>
      ) : (
        <div className={classes.configureBackupStorageConfigPrompt}>
          <Typography className={classes.promptPrimaryText} variant="body2">
            {t('configurePrompt.emptyMessage')}
          </Typography>
          <RbacValidator
            accessRequiredOn={ApiPermissionMap.ADD_CUSTOMER_CONFIG}
            isControl
            overrideStyle={{ display: 'block' }}
          >
            <YBButton
              className={classes.promptPrimaryActionButton}
              variant="secondary"
              onClick={openRedirectConfirmationModal}
              disabled={false}
              data-testid={`${COMPONENT_NAME}-ConfigureButton`}
            >
              {t('configurePrompt.initialCreateAction')}
            </YBButton>
          </RbacValidator>
        </div>
      )}

      <RedirectToStorageConfigConfigurationModal
        open={isRedirectConfirmationModalOpen}
        onClose={closeRedirectConfirmationModal}
      />
    </div>
  );
};

const getStorageConfigOptions = (customerConfigs: CustomerConfig[]) =>
  getStorageConfigs(customerConfigs).map((storageConfig) => {
    return {
      value: storageConfig.configUUID,
      label: storageConfig.configName
    };
  });

const StorageConfigMenuList = ({
  openRedirectConfirmationModal,
  children,
  ...props
}: MenuListComponentProps<BackupStorageConfigReactSelectOption> & {
  openRedirectConfirmationModal: () => void;
}) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  return (
    <components.MenuList {...props} className={classes.storageConfigMenuList}>
      <div style={{ maxHeight: '200px', overflowY: 'auto' }}>{children}</div>
      <div
        className={classes.storageConfigSelectCreateNewAction}
        onClick={openRedirectConfirmationModal}
      >
        <AddIcon />
        <Typography variant="body2">{t('selectComponent.additionalCreateAction')}</Typography>
      </div>
    </components.MenuList>
  );
};
