import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';

import InfoIcon from '../../../../redesign/assets/info-message.svg';
import { IStorageConfig as BackupStorageConfig } from '../../../backupv2';
import { I18N_KEY_PREFIX_XCLUSTER_TERMS, INPUT_FIELD_WIDTH_PX } from '../../constants';
import { BootstrapCategoryCard } from './BootstrapCategoryCard';
import { ReactSelectStorageConfigField } from '../ReactSelectStorageConfig';
import { RuntimeConfigKey } from '../../../../redesign/helpers/constants';
import { YBCheckboxField, YBTooltip } from '../../../../redesign/components';
import { api, runtimeConfigQueryKey } from '../../../../redesign/helpers/api';
import { YBBanner, YBBannerVariant } from '../../../common/descriptors';

import {
  CategorizedNeedBootstrapPerTableResponse,
  CategoryNeedBootstrapResponse
} from '../../XClusterTypes';

interface CommonConfigureBootstrapStepProps {
  categorizedNeedBootstrapPerTableResponse: CategorizedNeedBootstrapPerTableResponse | null;
  isFormDisabled: boolean;
  sourceUniverseUuid: string;
}

type ConfigureBootstrapStepProps =
  | (CommonConfigureBootstrapStepProps & {
      isDrInterface: true;
      storageConfigUuid: string;
    })
  | (CommonConfigureBootstrapStepProps & { isDrInterface: false });

const useStyles = makeStyles((theme) => ({
  stepContainer: {
    '& ol': {
      paddingLeft: theme.spacing(2),
      listStylePosition: 'outside',
      '& li::marker': {
        fontWeight: 'bold'
      }
    }
  },
  instruction: {
    marginBottom: theme.spacing(2)
  },
  formSectionDescription: {
    marginBottom: theme.spacing(3)
  },
  fieldLabel: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center',

    marginBottom: theme.spacing(1)
  },
  infoIcon: {
    '&:hover': {
      cursor: 'pointer'
    }
  },
  bootstrapCategoryGroup: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),

    width: 'fit',
    padding: `${theme.spacing(2)}px ${theme.spacing(2)}px`,

    border: `1px solid ${theme.palette.ybacolors.ybGray}`,
    borderRadius: '8px'
  },
  bootstrapCategoryCardContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1)
  },
  skipBootstrapCheckbox: {
    marginTop: 'auto'
  },
  bannerOverrides: {
    marginTop: theme.spacing(1)
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.shared.bootstrapSummary';

export const BootstrapSummary = (props: ConfigureBootstrapStepProps) => {
  const { categorizedNeedBootstrapPerTableResponse, isFormDisabled, sourceUniverseUuid } = props;
  const { control, watch } = useFormContext();
  const classes = useStyles();
  const theme = useTheme();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const customerUuid = localStorage.getItem('customerId') ?? '';
  const runtimeConfigQuery = useQuery(runtimeConfigQueryKey.customerScope(customerUuid), () =>
    api.fetchRuntimeConfigs(sourceUniverseUuid, true)
  );

  // Looking up storage config name for DR UI only.
  const storageConfigs: BackupStorageConfig[] = useSelector((reduxState: any) =>
    reduxState?.customer?.configs?.data.filter(
      (storageConfig: BackupStorageConfig) => storageConfig.type === 'STORAGE'
    )
  );
  const storageConfigName = props.isDrInterface
    ? storageConfigs?.find((storageConfig) => storageConfig.configUUID === props.storageConfigUuid)
        ?.configName ?? ''
    : '';

  if (categorizedNeedBootstrapPerTableResponse === null) {
    return <Typography variant="body2">{t('error.unableToDetermineBootstrapSummary')}</Typography>;
  }

  const {
    bootstrapTableUuids,
    noBootstrapRequired,
    tableHasDataBidirectional,
    targetTableMissingBidirectional,
    tableHasData,
    targetTableMissing
  } = categorizedNeedBootstrapPerTableResponse;
  const skipBootstrap = watch('skipBootstrap');
  const runtimeConfigEntries = runtimeConfigQuery.data.configEntries ?? [];

  const isSkipBootstrappingEnabled = runtimeConfigEntries.some(
    (config: any) =>
      config.key === RuntimeConfigKey.ENABLE_XCLUSTER_SKIP_BOOTSTRAPPING && config.value === 'true'
  );

  const singleDirectionBootstrapRequiredCategories = [tableHasData, targetTableMissing];

  const noBootstrapPlannedCategories = [
    noBootstrapRequired,
    tableHasDataBidirectional,
    targetTableMissingBidirectional
  ];
  const bootstrapPlannedCategories: CategoryNeedBootstrapResponse[] = [];
  if (skipBootstrap) {
    noBootstrapPlannedCategories.push(...singleDirectionBootstrapRequiredCategories);
  } else {
    bootstrapPlannedCategories.push(...singleDirectionBootstrapRequiredCategories);
  }

  const numTablesRequiringBootstrapInBidirectionalDb =
    tableHasDataBidirectional.tableCount + targetTableMissingBidirectional.tableCount;
  const isPossibleDataInconsistencyPresent =
    (bootstrapTableUuids.length > 0 && skipBootstrap) ||
    numTablesRequiringBootstrapInBidirectionalDb > 0;

  // Defining user facing product terms here.
  const sourceUniverseTerm = t(`source.${props.isDrInterface ? 'dr' : 'xClusterReplication'}`, {
    keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
  });
  const targetUniverseTerm = t(`target.${props.isDrInterface ? 'dr' : 'xClusterReplication'}`, {
    keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
  });
  return (
    <>
      {bootstrapTableUuids.length > 0 && !skipBootstrap && (
        <>
          <Typography variant="body2" className={classes.instruction}>
            {t('backupStorageConfig.instruction')}
          </Typography>
          <div className={classes.formSectionDescription}>
            <Typography variant="body2">{t('backupStorageConfig.infoText')}</Typography>
          </div>
          <div className={classes.fieldLabel}>
            <Typography variant="body2">{t('backupStorageConfig.label')}</Typography>
            <YBTooltip
              title={
                <Typography variant="body2">
                  <Trans
                    i18nKey={`${TRANSLATION_KEY_PREFIX}.backupStorageConfig.tooltip`}
                    components={{ paragraph: <p />, bold: <b /> }}
                    values={{
                      sourceUniverseTerm: sourceUniverseTerm,
                      targetUniverseTerm: targetUniverseTerm
                    }}
                  />
                </Typography>
              }
            >
              <img src={InfoIcon} alt={t('infoIcon', { keyPrefix: 'imgAltText' })} />
            </YBTooltip>
          </div>
          {/* Backup storage config should already be saved for each DR config. */}
          {props.isDrInterface ? (
            storageConfigName ? (
              <Typography variant="body2">
                <Trans
                  i18nKey={`${TRANSLATION_KEY_PREFIX}.backupStorageConfig.currentStorageConfigInfo`}
                  components={{ bold: <b /> }}
                  values={{ storageConfigName: storageConfigName }}
                />
              </Typography>
            ) : (
              <Typography variant="body2">
                {t('backupStorageConfig.missingStorageConfigInfo')}
              </Typography>
            )
          ) : (
            <ReactSelectStorageConfigField
              control={control}
              name="storageConfig"
              rules={{ required: t('error.backupStorageConfigRequired') }}
              isDisabled={isFormDisabled}
              autoSizeMinWidth={INPUT_FIELD_WIDTH_PX}
              maxWidth="100%"
            />
          )}
        </>
      )}
      <Box marginTop={3} display="flex" gridGap={theme.spacing(3)}>
        <div>
          <div className={classes.bootstrapCategoryGroup}>
            <Typography variant="body1">{t('categoryGroup.noBootstrapPlanned')}</Typography>
            <div className={classes.bootstrapCategoryCardContainer}>
              {noBootstrapPlannedCategories.map((category) => (
                <>
                  {category.tableCount > 0 && (
                    <BootstrapCategoryCard
                      categoryNeedBootstrapResponse={category}
                      isBootstrapPlanned={false}
                      isDrInterface={props.isDrInterface}
                    />
                  )}
                </>
              ))}
            </div>
          </div>
        </div>
        {bootstrapTableUuids.length > 0 && !skipBootstrap && (
          <div className={classes.bootstrapCategoryGroup}>
            <Typography variant="body1">{t('categoryGroup.bootstrapPlanned')}</Typography>
            <div className={classes.bootstrapCategoryCardContainer}>
              {bootstrapPlannedCategories.map((category) => (
                <>
                  {category.tableCount > 0 && (
                    <BootstrapCategoryCard
                      categoryNeedBootstrapResponse={category}
                      isBootstrapPlanned={true}
                      isDrInterface={props.isDrInterface}
                    />
                  )}
                </>
              ))}
            </div>
          </div>
        )}
      </Box>
      {isSkipBootstrappingEnabled && (
        <YBCheckboxField
          className={classes.skipBootstrapCheckbox}
          control={control}
          name="skipBootstrap"
          label={t('skipBootstrap')}
        />
      )}
      {isPossibleDataInconsistencyPresent && (
        <YBBanner variant={YBBannerVariant.WARNING} className={classes.bannerOverrides}>
          <Typography variant="body2" component="p">
            {t('possibleDataInconsistencyWarning')}
          </Typography>
          {numTablesRequiringBootstrapInBidirectionalDb > 0 && (
            <Typography variant="body2" component="p">
              {t('bidirectionalReplicationSkippedBootstrappingWarning')}
            </Typography>
          )}
        </YBBanner>
      )}
      {targetTableMissing.tableCount > 0 && (
        <YBBanner variant={YBBannerVariant.INFO} className={classes.bannerOverrides}>
          <Typography variant="body2" component="p">
            {t('avoidBootstrapForMIssingTablesTip', {
              targetUniverseTerm: targetUniverseTerm
            })}
          </Typography>
        </YBBanner>
      )}
    </>
  );
};
