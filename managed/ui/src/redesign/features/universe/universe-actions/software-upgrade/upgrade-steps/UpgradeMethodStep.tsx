import { Box, makeStyles, Typography, Link as MuiLink } from '@material-ui/core';
import clsx from 'clsx';
import { useFormContext } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { YBTag } from '@yugabyte-ui-library/core';

import { YBTooltip } from '@app/redesign/components';
import { YBRadio } from '@app/redesign/components/YBRadio/YBRadio';
import {
  YBA_UNIVERSE_UPGRADE_DOCUMENTATION_URL,
  UpgradeMethod,
  UpgradePace
} from '@app/redesign/features/universe/universe-actions/software-upgrade/constants';
import { useDbUpgradeModalContext } from '@app/redesign/features/universe/universe-actions/software-upgrade/DbUpgradeModalContext';
import type { DBUpgradeFormFields } from '@app/redesign/features/universe/universe-actions/software-upgrade/types';
import { getIsCanaryUpgradeAvailable } from '@app/redesign/features/universe/universe-actions/software-upgrade/utils/formUtils';
import { precheckSoftwareUpgrade } from '@app/v2/api/universe/universe';
import { RollingUpdateBatchSettings } from '../components/RollingUpdateBatchSettings';
import { usePillStyles } from '@app/redesign/styles/styles';

import CircleCheckedIcon from '@app/redesign/assets/circle-checked.svg';
import CircleUnselectedIcon from '@app/redesign/assets/circle-unselected.svg';
import TreeIcon from '@app/redesign/assets/tree-icon.svg';
import ErrorIcon from '@app/redesign/assets/approved/error.svg';

const useStyles = makeStyles((theme) => ({
  stepContainer: {
    display: 'flex',
    flex: 1,
    flexDirection: 'column',

    overflow: 'auto'
  },
  stepHeader: {
    padding: theme.spacing(3),

    color: theme.palette.grey[900],
    fontSize: 15,
    fontWeight: 600,
    lineHeight: '20px'
  },
  stepBody: {
    display: 'flex',
    flexDirection: 'column',

    padding: theme.spacing(0.5, 3, 4, 3)
  },

  expressSection: {
    display: 'flex',
    flexDirection: 'column',

    borderLeft: `1px solid ${theme.palette.grey[300]}`,
    borderRight: `1px solid ${theme.palette.grey[300]}`,
    borderTop: `1px solid ${theme.palette.grey[300]}`,
    borderTopLeftRadius: theme.shape.borderRadius,
    borderTopRightRadius: theme.shape.borderRadius,
    cursor: 'pointer',

    '&$selectedSection': {
      backgroundColor: theme.palette.ybacolors.grey005
    }
  },
  selectedSection: {},
  expressHeader: {
    display: 'flex',
    flexDirection: 'column',
    gap: 4,

    padding: theme.spacing(2.5, 2)
  },
  canarySection: {
    display: 'flex',
    flexDirection: 'column',
    gap: 4,

    padding: theme.spacing(2.5, 2),

    backgroundColor: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[300]}`,
    borderBottomLeftRadius: theme.shape.borderRadius,
    borderBottomRightRadius: theme.shape.borderRadius,
    cursor: 'pointer',

    '&$selectedSection': {
      backgroundColor: theme.palette.ybacolors.grey005
    }
  },

  settingContainer: {
    display: 'flex',
    flexDirection: 'column'
  },

  methodHeader: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(2)
  },
  methodIcon: {
    flexShrink: 0,

    display: 'inline-flex',

    width: 24,
    height: 24,

    '& svg': {
      width: 24,
      height: 24
    }
  },
  methodTitle: {
    color: theme.palette.grey[900],
    fontSize: 13,
    fontWeight: 600,
    lineHeight: '16px'
  },
  methodDescription: {
    paddingLeft: 40,

    color: theme.palette.grey[700],
    fontSize: '11.5px',
    fontWeight: 400,
    lineHeight: '18px'
  },
  methodTag: {
    display: 'inline-flex',
    alignItems: 'center',

    padding: '2px 6px',

    backgroundColor: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[300]}`,
    borderRadius: 4,
    color: theme.palette.grey[900],
    fontSize: '11.5px',
    lineHeight: '16px'
  },

  expandedContent: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),

    paddingBottom: theme.spacing(3),
    paddingLeft: 50,
    paddingRight: theme.spacing(3)
  },

  upgradePaceCard: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),

    padding: theme.spacing(2),
    paddingBottom: theme.spacing(2.5),
    overflow: 'clip',

    backgroundColor: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius
  },
  upgradePaceTitle: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1)
  },
  upgradePaceLabel: {
    color: theme.palette.grey[700],
    fontSize: '11.5px',
    fontWeight: 500,
    lineHeight: '16px',
    textTransform: 'uppercase'
  },
  paceOptionsContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1),

    paddingLeft: theme.spacing(4)
  },
  paceOption: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    padding: theme.spacing(1, 0),

    cursor: 'pointer'
  },
  paceOptionLabel: {
    color: theme.palette.grey[900],
    fontSize: 13,
    fontWeight: 400,
    lineHeight: '16px'
  },
  rollingSettingsWrapper: {
    paddingLeft: theme.spacing(4)
  },

  concurrentUpgradeWarningBanner: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    padding: theme.spacing(1),

    backgroundColor: theme.palette.error[50],
    border: `1px solid ${theme.palette.error[100]}`,
    borderRadius: theme.shape.borderRadius,
    color: theme.palette.grey[900],
    fontSize: 11.5,
    fontWeight: 400,
    lineHeight: '16px'
  },
  concurrentUpgradeWarningIcon: {
    color: theme.palette.error[500]
  },

  recommendationBanner: {
    display: 'flex',
    alignItems: 'flex-start',
    gap: theme.spacing(1)
  },
  recommendationText: {
    paddingTop: 2.5,

    color: theme.palette.grey[900],
    fontSize: 13,
    fontWeight: 400,
    lineHeight: '19px'
  },
  canaryOptionDisabled: {
    opacity: 0.4,
    cursor: 'not-allowed'
  },
  canaryTooltipLink: {
    color: theme.palette.primary.main
  }
}));

const TRANSLATION_KEY_PREFIX = 'universeActions.dbUpgrade.upgradeModal.upgradeMethodStep';
const TEST_ID_PREFIX = 'UpgradeMethodStep';

export const UpgradeMethodStep = () => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const pillClasses = usePillStyles();
  const { control, watch, setValue, clearErrors, trigger, formState } =
    useFormContext<DBUpgradeFormFields>();
  const { maxNodesPerBatchMaximum, clusters, currentUniverseUuid } = useDbUpgradeModalContext();
  const targetDbVersion = watch('targetDbVersion');

  const dbUpgradeMetaQuery = useQuery(
    ['softwareUpgradePrecheck', currentUniverseUuid, targetDbVersion],
    () =>
      precheckSoftwareUpgrade(currentUniverseUuid, {
        yb_software_version: targetDbVersion
      }),
    { enabled: !!currentUniverseUuid && !!targetDbVersion }
  );
  const isYsqlMajorUpgrade = dbUpgradeMetaQuery.data?.ysql_major_version_upgrade ?? false;

  const selectedMethod = watch('upgradeMethod');
  const selectedPace = watch('upgradePace');
  const isFormDisabled = formState.isSubmitting;
  const isCanaryUpgradeAvailable = getIsCanaryUpgradeAvailable(clusters);

  const handleMethodSelect = (method: UpgradeMethod) => {
    if (isFormDisabled) {
      return;
    }
    if (method === UpgradeMethod.CANARY && !isCanaryUpgradeAvailable) {
      return;
    }
    setValue('upgradeMethod', method);
    if (method === UpgradeMethod.CANARY) {
      // Only rolling upgrade is supported when upgradeMethod is CANARY.
      setValue('upgradePace', UpgradePace.ROLLING);
    }
  };

  const handleUpgradePaceChange = (pace: UpgradePace) => {
    setValue('upgradePace', pace);
    if (pace === UpgradePace.CONCURRENT) {
      clearErrors(['maxNodesPerBatch', 'waitBetweenBatchesSeconds']);
    } else {
      trigger(['maxNodesPerBatch', 'waitBetweenBatchesSeconds']);
    }
  };

  return (
    <div className={classes.stepContainer}>
      <Typography className={classes.stepHeader} variant="h5">
        {t('stepTitle')}
      </Typography>

      <div className={classes.stepBody}>
        <div className={classes.settingContainer}>
          <div
            className={clsx(
              classes.expressSection,
              selectedMethod === UpgradeMethod.EXPRESS && classes.selectedSection
            )}
            onClick={() => handleMethodSelect(UpgradeMethod.EXPRESS)}
            data-testid={`${TEST_ID_PREFIX}-ExpressOption`}
          >
            <div className={classes.expressHeader}>
              <div className={classes.methodHeader}>
                <span className={classes.methodIcon}>
                  {selectedMethod === UpgradeMethod.EXPRESS ? (
                    <CircleCheckedIcon />
                  ) : (
                    <CircleUnselectedIcon />
                  )}
                </span>
                <Typography className={classes.methodTitle}>{t('express.title')}</Typography>
              </div>
              <Typography className={classes.methodDescription}>
                {t('express.description')}
              </Typography>
            </div>

            {selectedMethod === UpgradeMethod.EXPRESS && (
              <div className={classes.expandedContent} onClick={(event) => event.stopPropagation()}>
                <div className={classes.upgradePaceCard}>
                  <div className={classes.upgradePaceTitle}>
                    <TreeIcon />
                    <Typography className={classes.upgradePaceLabel}>
                      {t('upgradePace.title')}
                    </Typography>
                  </div>
                  <div className={classes.paceOptionsContainer}>
                    <div>
                      <div
                        className={classes.paceOption}
                        onClick={() => {
                          if (!isFormDisabled) handleUpgradePaceChange(UpgradePace.ROLLING);
                        }}
                        onKeyDown={(event) => {
                          if ((event.key === 'Enter' || event.key === ' ') && !isFormDisabled) {
                            handleUpgradePaceChange(UpgradePace.ROLLING);
                          }
                        }}
                        role="button"
                      >
                        <YBRadio
                          checked={selectedPace === UpgradePace.ROLLING}
                          disabled={isFormDisabled}
                          inputProps={{
                            'data-testid': `${TEST_ID_PREFIX}-RollingRadio`
                          }}
                          onChange={() => handleUpgradePaceChange(UpgradePace.ROLLING)}
                        />
                        <Typography className={classes.paceOptionLabel}>
                          {t('upgradePace.rolling')}
                        </Typography>
                      </div>

                      <div className={classes.rollingSettingsWrapper}>
                        <RollingUpdateBatchSettings<DBUpgradeFormFields>
                          control={control}
                          errors={formState.errors}
                          maxNodesPerBatchName="maxNodesPerBatch"
                          waitBetweenBatchesName="waitBetweenBatchesSeconds"
                          maxNodesPerBatchMaximum={maxNodesPerBatchMaximum}
                          shouldValidate={(formValues) =>
                            formValues.upgradePace === UpgradePace.ROLLING
                          }
                          isDisabled={isFormDisabled || selectedPace === UpgradePace.CONCURRENT}
                          testIdPrefix={TEST_ID_PREFIX}
                        />
                      </div>
                    </div>
                    <div
                      className={classes.paceOption}
                      onClick={() => {
                        if (!isFormDisabled) handleUpgradePaceChange(UpgradePace.CONCURRENT);
                      }}
                      onKeyDown={(event) => {
                        if ((event.key === 'Enter' || event.key === ' ') && !isFormDisabled) {
                          handleUpgradePaceChange(UpgradePace.CONCURRENT);
                        }
                      }}
                      role="button"
                    >
                      <YBRadio
                        checked={selectedPace === UpgradePace.CONCURRENT}
                        disabled={isFormDisabled}
                        inputProps={{
                          'data-testid': `${TEST_ID_PREFIX}-ConcurrentRadio`
                        }}
                        onChange={() => handleUpgradePaceChange(UpgradePace.CONCURRENT)}
                      />
                      <Typography className={classes.paceOptionLabel}>
                        {t('upgradePace.concurrent')}
                      </Typography>
                    </div>
                    {selectedPace === UpgradePace.CONCURRENT && (
                      <div className={classes.concurrentUpgradeWarningBanner}>
                        <ErrorIcon
                          className={classes.concurrentUpgradeWarningIcon}
                          width={24}
                          height={24}
                        />
                        <Typography variant="subtitle1" component="span">
                          <Trans
                            t={t}
                            i18nKey="concurrentUpgradeDowntimeWarning"
                            components={{
                              bold: <Typography variant="subtitle2" component="span" />
                            }}
                          />
                        </Typography>
                      </div>
                    )}
                  </div>
                </div>
                {isYsqlMajorUpgrade && isCanaryUpgradeAvailable && (
                  <div className={classes.recommendationBanner}>
                    <div className={clsx(pillClasses.pill, pillClasses.purple)}>
                      {t('recommendationLabel')}
                    </div>
                    <Typography className={classes.recommendationText}>
                      {t('recommendationMessage')}
                    </Typography>
                  </div>
                )}
              </div>
            )}
          </div>

          {/* Canary Upgrade Section */}
          <YBTooltip
            placement="top"
            interactive
            title={
              isCanaryUpgradeAvailable ? (
                ''
              ) : (
                <Trans
                  t={t}
                  i18nKey="canary.unavailableTooltip"
                  components={{
                    dbUpgradeRequirementsLink: (
                      <MuiLink
                        className={classes.canaryTooltipLink}
                        target="_blank"
                        rel="noopener noreferrer"
                        href={YBA_UNIVERSE_UPGRADE_DOCUMENTATION_URL}
                      />
                    )
                  }}
                />
              )
            }
          >
            <div
              className={clsx(
                classes.canarySection,
                selectedMethod === UpgradeMethod.CANARY && classes.selectedSection,
                !isCanaryUpgradeAvailable && classes.canaryOptionDisabled
              )}
              onClick={() => handleMethodSelect(UpgradeMethod.CANARY)}
              data-testid={`${TEST_ID_PREFIX}-CanaryOption`}
            >
              <div className={classes.methodHeader}>
                <span className={classes.methodIcon}>
                  {selectedMethod === UpgradeMethod.CANARY ? (
                    <CircleCheckedIcon />
                  ) : (
                    <CircleUnselectedIcon />
                  )}
                </span>
                <Box display="flex" alignItems="center" style={{ gap: 8 }}>
                  <Typography className={classes.methodTitle}>{t('canary.title')}</Typography>
                  <YBTag size="small" variant="light">
                    {t('canary.tag')}
                  </YBTag>
                </Box>
              </div>

              <Typography className={classes.methodDescription}>
                {t('canary.description')}
              </Typography>
            </div>
          </YBTooltip>
        </div>
      </div>
    </div>
  );
};
