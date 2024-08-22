import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { useFormContext } from 'react-hook-form';
import { useQuery } from 'react-query';
import { Trans, useTranslation } from 'react-i18next';
import { useState } from 'react';

import { RuntimeConfigKey, UnavailableUniverseStates } from '../../../redesign/helpers/constants';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { YBReactSelectField } from '../../configRedesign/providerRedesign/components/YBReactSelect/YBReactSelectField';
import { api, runtimeConfigQueryKey, universeQueryKey } from '../../../redesign/helpers/api';
import { getPrimaryCluster } from '../../../utils/universeUtilsTyped';
import { getUniverseStatus } from '../../universes/helpers/universeHelpers';
import { CollapsibleNote } from '../sharedComponents/CollapsibleNote';
import { CreateXClusterConfigFormValues } from './CreateConfigModal';
import { YBCheckboxField, YBInputField, YBTooltip } from '../../../redesign/components';
import {
  INPUT_FIELD_WIDTH_PX,
  XCLUSTER_CONFIG_NAME_ILLEGAL_PATTERN,
  XCLUSTER_REPLICATION_DOCUMENTATION_URL,
  YB_ADMIN_XCLUSTER_DOCUMENTATION_URL
} from '../constants';
import { getIsTransactionalAtomicityEnabled } from '../ReplicationUtils';
import { YBBanner, YBBannerVariant } from '../../common/descriptors';
import InfoMessageIcon from '../../../redesign/assets/info-message.svg';
import { hasNecessaryPerm } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';

import { TableType, TableTypeLabel, Universe } from '../../../redesign/helpers/dtos';

interface SelectTargetUniverseStepProps {
  isFormDisabled: boolean;
  sourceUniverse: Universe;
}

const useStyles = makeStyles((theme) => ({
  stepContainer: {
    display: 'flex',
    flexDirection: 'column',

    '& ol': {
      paddingLeft: theme.spacing(2),
      listStylePosition: 'outside',
      '& li::marker': {
        fontWeight: 'bold'
      }
    }
  },
  instruction: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    marginBottom: theme.spacing(4)
  },
  inputField: {
    width: INPUT_FIELD_WIDTH_PX
  },
  fieldGroup: {},
  fieldLabel: {
    marginBottom: theme.spacing(1)
  },
  fieldHelpText: {
    marginTop: theme.spacing(1),

    color: theme.palette.ybacolors.textGray,
    fontSize: '12px'
  },
  icon: {
    '&:hover': {
      cursor: 'pointer'
    }
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.createConfigModal.step.selectTargetUniverse';
const TABLE_TYPE_OPTIONS = [
  {
    value: TableType.PGSQL_TABLE_TYPE,
    label: TableTypeLabel[TableType.PGSQL_TABLE_TYPE]
  },
  { value: TableType.YQL_TABLE_TYPE, label: TableTypeLabel[TableType.YQL_TABLE_TYPE] }
] as const;

/**
 * Component requirements:
 * - An ancestor must provide form context using <FormProvider> from React Hook Form
 */
export const SelectTargetUniverseStep = ({
  isFormDisabled,
  sourceUniverse
}: SelectTargetUniverseStepProps) => {
  const [isTooltipOpen, setIsTooltipOpen] = useState<boolean>(false);
  const [isMouseOverTooltip, setIsMouseOverTooltip] = useState<boolean>(false);
  const { control, watch, setValue } = useFormContext<CreateXClusterConfigFormValues>();
  const classes = useStyles();
  const theme = useTheme();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const universeListQuery = useQuery<Universe[]>(universeQueryKey.ALL, () =>
    api.fetchUniverseList()
  );
  const runtimeConfigQuery = useQuery(
    runtimeConfigQueryKey.universeScope(sourceUniverse.universeUUID),
    () => api.fetchRuntimeConfigs(sourceUniverse.universeUUID, true)
  );

  if (
    universeListQuery.isLoading ||
    universeListQuery.isIdle ||
    runtimeConfigQuery.isLoading ||
    runtimeConfigQuery.isIdle
  ) {
    return <YBLoading />;
  }

  if (universeListQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchUniverseList', { keyPrefix: 'queryError' })}
      />
    );
  }

  if (runtimeConfigQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchUniverseRuntimeConfig', { keyPrefix: 'queryError' })}
      />
    );
  }

  const universeOptions = universeListQuery.data
    .filter(
      (universe) =>
        universe.universeUUID !== sourceUniverse.universeUUID &&
        !UnavailableUniverseStates.includes(getUniverseStatus(universe).state)
    )
    .map((universe) => {
      const isDisabled = !hasNecessaryPerm({
        ...ApiPermissionMap.CREATE_XCLUSTER_REPLICATION,
        onResource: universe.universeUUID
      });
      return {
        label: universe.name,
        value: universe,
        isDisabled: isDisabled,
        disabledReason: isDisabled ? t('missingPermissionOnUniverse') : ''
      };
    });

  const runtimeConfigEntries = runtimeConfigQuery.data.configEntries ?? [];
  const isTransactionalAtomicityEnabled = runtimeConfigEntries.some(
    (config: any) =>
      config.key === RuntimeConfigKey.XCLUSTER_TRANSACTIONAL_ATOMICITY_FEATURE_FLAG &&
      config.value === 'true'
  );
  // targetUniverse could be undefined on this page since the user might not have entered a value yet.
  const targetUniverse = watch('targetUniverse') as { label: string; value: Universe } | undefined;
  const isTransactionalAtomicitySupported = getIsTransactionalAtomicityEnabled(
    sourceUniverse,
    targetUniverse?.value
  );
  const tableType = watch('tableType')?.value;
  const isTransactionalConfig = watch('isTransactionalConfig');
  if (
    isTransactionalConfig &&
    (!isTransactionalAtomicitySupported || !isTransactionalAtomicityEnabled)
  ) {
    // `isTransactionalConfig` is `true` by default.
    // We set this value to false whenever transactional atomicity is not supported by
    // the current field selection.
    setValue('isTransactionalConfig', false);
  }
  return (
    <div className={classes.stepContainer}>
      <ol>
        <li>
          <Typography variant="body1" className={classes.instruction}>
            {t('instruction.text')}
          </Typography>
          <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
            <div>
              <Typography variant="body2" className={classes.fieldLabel}>
                {t('field.configName')}
              </Typography>
              <YBInputField
                className={classes.inputField}
                control={control}
                name="configName"
                disabled={isFormDisabled}
                rules={{
                  validate: {
                    required: (configName) => !!configName || t('error.requiredField'),
                    noIllegalCharactersInConfigName: (configName: any) => {
                      return (
                        !XCLUSTER_CONFIG_NAME_ILLEGAL_PATTERN.test(configName) ||
                        t('error.illegalCharactersInConfigName')
                      );
                    }
                  }
                }}
              />
            </div>
            <div>
              <Typography variant="body2" className={classes.fieldLabel}>
                {t('field.targetUniverse')}
              </Typography>
              <YBReactSelectField
                control={control}
                name="targetUniverse"
                options={universeOptions}
                autoSizeMinWidth={INPUT_FIELD_WIDTH_PX}
                maxWidth="100%"
                rules={{
                  validate: {
                    required: (targetUniverse) => !!targetUniverse || t('error.requiredField'),
                    hasMatchingTLSConfiguration: (targetUniverse: any) =>
                      getPrimaryCluster(targetUniverse.value.universeDetails.clusters)?.userIntent
                        ?.enableNodeToNodeEncrypt ===
                        getPrimaryCluster(sourceUniverse.universeDetails.clusters)?.userIntent
                          ?.enableNodeToNodeEncrypt || t('error.mismatchedNodeToNodeEncryption')
                  }
                }}
                isDisabled={isFormDisabled}
              />
            </div>
            <div>
              <Typography variant="body2" className={classes.fieldLabel}>
                {t('field.tableType')}
              </Typography>
              <YBReactSelectField
                control={control}
                name="tableType"
                options={TABLE_TYPE_OPTIONS}
                autoSizeMinWidth={INPUT_FIELD_WIDTH_PX}
                maxWidth="100%"
                rules={{
                  validate: {
                    required: (tableType) => !!tableType || t('error.requiredField')
                  }
                }}
                isDisabled={isFormDisabled}
                onChange={(option) => {
                  if (
                    option.value === TableType.PGSQL_TABLE_TYPE &&
                    isTransactionalAtomicitySupported
                  ) {
                    setValue('isTransactionalConfig', true);
                  } else {
                    setValue('isTransactionalConfig', false);
                  }
                }}
              />
            </div>
            {isTransactionalAtomicityEnabled && tableType === TableType.PGSQL_TABLE_TYPE && (
              <Box display="flex" gridGap="5px">
                <YBCheckboxField
                  control={control}
                  name="isTransactionalConfig"
                  label={t('field.isTransactionalConfig')}
                  disabled={!isTransactionalAtomicitySupported}
                />
                {/* This tooltip needs to be have a z-index greater than the z-index on the modal (3100)*/}
                <YBTooltip
                  open={isTooltipOpen || isMouseOverTooltip}
                  title={
                    <Trans
                      i18nKey={`${TRANSLATION_KEY_PREFIX}.isTransactionalConfigTooltip`}
                      components={{
                        paragraph: <p />,
                        bold: <b />,
                        orderedList: <ol />,
                        listItem: <li />,
                        xClusterDocLink: (
                          <a
                            href={XCLUSTER_REPLICATION_DOCUMENTATION_URL}
                            target="_blank"
                            rel="noopener noreferrer"
                          />
                        )
                      }}
                    />
                  }
                  PopperProps={{ style: { zIndex: 4000, pointerEvents: 'auto' } }}
                >
                  <img
                    className={classes.icon}
                    alt="Info"
                    src={InfoMessageIcon}
                    onClick={() => setIsTooltipOpen(!isTooltipOpen)}
                    onMouseOver={() => setIsMouseOverTooltip(true)}
                    onMouseOut={() => setIsMouseOverTooltip(false)}
                  />
                </YBTooltip>
              </Box>
            )}
          </Box>
        </li>
      </ol>
      <Box display="flex" flexDirection="column" gridGap={theme.spacing(1)} marginTop={4}>
        {/* Txn xCluster is only available for YSQL. We don't warn for YCQL as we don't offer an alternative. */}
        {!isTransactionalConfig && tableType === TableType.PGSQL_TABLE_TYPE && (
          <YBBanner variant={YBBannerVariant.WARNING}>
            <Typography variant="body2" component="p">
              {t('warning.nonTxnXCluster')}
            </Typography>
          </YBBanner>
        )}
        <CollapsibleNote
          noteContent={
            <Trans
              i18nKey={`${TRANSLATION_KEY_PREFIX}.backupStorageConfigNote`}
              components={{
                paragraph: <p />,
                bold: <b />,
                ybAdminDocsLink: (
                  <a
                    href={YB_ADMIN_XCLUSTER_DOCUMENTATION_URL}
                    target="_blank"
                    rel="noopener noreferrer"
                  />
                )
              }}
            />
          }
          expandContent={
            <>
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX}.fullCopyNote`}
                components={{ paragraph: <p />, bold: <b /> }}
              />
            </>
          }
        />
      </Box>
    </div>
  );
};
