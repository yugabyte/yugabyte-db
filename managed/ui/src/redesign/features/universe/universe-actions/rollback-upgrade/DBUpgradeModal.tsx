import { FC, ChangeEvent, FocusEvent, useState } from 'react';
import _ from 'lodash';
import { toast } from 'react-toastify';
import { useDispatch, useSelector } from 'react-redux';
import { useMutation, useQuery } from 'react-query';
import { useUpdateEffect } from 'react-use';
import { useTranslation } from 'react-i18next';
import { useForm, FormProvider, Controller } from 'react-hook-form';
import { Box, Typography, Tooltip } from '@material-ui/core';
import { fetchGlobalRunTimeConfigs } from '../../../../../api/admin';
import {
  YBModal,
  YBAutoComplete,
  YBCheckboxField,
  YBLabel,
  YBInputField
} from '../../../../components';
import { api } from '../../../../utils/api';
import {
  getPrimaryCluster,
  createErrorMessage,
  transitToUniverse,
  getAsyncCluster
} from '../../universe-form/utils/helpers';
import { Universe } from '../../universe-form/utils/dto';
import { fetchUniverseInfo, fetchUniverseInfoResponse } from '../../../../../actions/universe';
import {
  fetchCustomerTasks,
  fetchCustomerTasksSuccess,
  fetchCustomerTasksFailure
} from '../../../../../actions/tasks';
import { DBUpgradeFormFields, UPGRADE_TYPE, DBUpgradePayload } from './utils/types';
import { TOAST_AUTO_DISMISS_INTERVAL } from '../../universe-form/utils/constants';
import { fetchLatestStableVersion, fetchCurrentLatestVersion } from './utils/helper';
import {
  compareYBSoftwareVersions,
  isVersionStable
} from '../../../../../utils/universeUtilsTyped';
//Rbac
import { RBAC_ERR_MSG_NO_PERM } from '../../../rbac/common/validator/ValidatorUtils';
import { hasNecessaryPerm } from '../../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../rbac/ApiAndUserPermMapping';
//imported styles
import { dbUpgradeFormStyles } from './utils/RollbackUpgradeStyles';
//icons
import BulbIcon from '../../../../assets/bulb.svg';
import ExclamationIcon from '../../../../assets/exclamation-traingle.svg';
import { ReactComponent as UpgradeArrow } from '../../../../assets/upgrade-arrow.svg';
import WarningIcon from '../../../../assets/warning-triangle.svg';
import { YBLoadingCircleIcon } from '../../../../../components/common/indicators';
import { isNonEmptyString } from '../../../../../utils/ObjectUtils';
import { RuntimeConfigKey } from '../../../../helpers/constants';

import InfoMessageIcon from '../../../../../redesign/assets/info-message.svg';

interface DBUpgradeModalProps {
  open: boolean;
  onClose: () => void;
  universeData: Universe;
  isReleasesEnabled: boolean;
}

const MAX_RELEASE_TAG_CHAR = 20;
const TOAST_OPTIONS = { autoClose: TOAST_AUTO_DISMISS_INTERVAL };
const MINIMUM_SUPPORTED_VERSION = '2.20.2';

const renderOption = (option: Record<string, any>) => {
  return (
    <Box
      style={{
        display: 'flex',
        flexDirection: 'row'
      }}
    >
      {option.version}
      {isNonEmptyString(option.info.release_tag) && (
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
              {option.info.release_tag.length > MAX_RELEASE_TAG_CHAR
                ? `${option.info.release_tag.substring(0, 10)}...`
                : option.info.release_tag}
            </span>
          </Box>
          <span>
            {option.info.release_tag.length > MAX_RELEASE_TAG_CHAR && (
              <Tooltip title={option.info.release_tag} arrow placement="top">
                <img src={InfoMessageIcon} alt="info" />
              </Tooltip>
            )}
          </span>
        </>
      )}
    </Box>
  );
};

export const DBUpgradeModal: FC<DBUpgradeModalProps> = ({
  open,
  onClose,
  universeData,
  isReleasesEnabled
}) => {
  const { t } = useTranslation();
  const classes = dbUpgradeFormStyles();
  const [needPrefinalize, setPrefinalize] = useState(false);
  const releases = useSelector((state: any) =>
    isReleasesEnabled
      ? state.customer.dbVersionsWithMetadata
      : state.customer.softwareVersionswithMetaData
  );
  const featureFlags = useSelector((state: any) => state.featureFlags);
  const { universeDetails, universeUUID, rollMaxBatchSize } = universeData;
  const primaryCluster = _.cloneDeep(getPrimaryCluster(universeDetails));
  const currentReleaseFromCluster = primaryCluster?.userIntent.ybSoftwareVersion;
  let currentRelease = '';
  if (currentReleaseFromCluster !== null && currentReleaseFromCluster !== undefined) {
    currentRelease = currentReleaseFromCluster;
  }
  const universeHasXcluster =
    universeData?.universeDetails?.xclusterInfo?.sourceXClusterConfigs?.length > 0 ||
    universeData?.universeDetails?.xclusterInfo?.targetXClusterConfigs?.length > 0;

  const { data: globalRuntimeConfigs, isLoading } = useQuery(['globalRuntimeConfigs'], () =>
    fetchGlobalRunTimeConfigs(true).then((res: any) => res.data)
  );

  const skipVersionChecksValue = globalRuntimeConfigs?.configEntries?.find(
    (c: any) => c.key === RuntimeConfigKey.SKIP_VERSION_CHECKS
  )?.value;
  const isRollingUpradeMutlipleNodesEnabled =
    globalRuntimeConfigs?.configEntries?.find(
      (c: any) => c.key === RuntimeConfigKey.BATCH_ROLLING_UPGRADE_FEATURE_FLAG
    )?.value === 'true';
  // By default skipVersionChecks is false
  // If runtime config flag is not accessible, assign false to the variable
  const skipVersionChecks =
    skipVersionChecksValue === undefined || skipVersionChecksValue === 'false' ? false : true;

  let finalOptions: Record<string, any>[] = [];
  const latestStableVersion = fetchLatestStableVersion(releases);
  const latestCurrentRelease = fetchCurrentLatestVersion(releases, currentRelease);
  const isCurrentReleaseStable = isVersionStable(currentRelease);
  // Add latest stable version when current release is stable or when skipVersionCheck is true
  if (
    latestStableVersion &&
    compareYBSoftwareVersions({
      versionA: latestStableVersion.version,
      versionB: currentRelease,
      options: {
        suppressFormatError: true
      }
    }) >= 0 &&
    (isCurrentReleaseStable || skipVersionChecks)
  )
    finalOptions = [latestStableVersion];
  if (latestCurrentRelease) finalOptions = [...finalOptions, latestCurrentRelease];
  let sortedVersions: string[];
  const stableSortedVersions = Object.keys(releases)
    .filter((release) => isVersionStable(release))
    .sort((versionA, versionB) =>
      compareYBSoftwareVersions({
        versionA: versionB,
        versionB: versionA,
        options: {
          suppressFormatError: true,
          requireOrdering: true
        }
      })
    );

  const previewSortedVersions = Object.keys(releases)
    .filter((release) => !isVersionStable(release))
    .sort((versionA, versionB) =>
      compareYBSoftwareVersions({
        versionA: versionB,
        versionB: versionA,
        options: {
          suppressFormatError: true,
          requireOrdering: true
        }
      })
    );

  let currentReleaseIndex = 0;
  let versionsAboveCurrent: string[] = [];
  if (!skipVersionChecks) {
    if (isCurrentReleaseStable) {
      sortedVersions = stableSortedVersions;
    } else {
      sortedVersions = previewSortedVersions;
    }
    currentReleaseIndex = sortedVersions.indexOf(currentRelease ?? '');
    versionsAboveCurrent = sortedVersions.slice(0, currentReleaseIndex + 1);
  } else {
    sortedVersions = Object.keys(releases).sort((versionA, versionB) =>
      compareYBSoftwareVersions({
        versionA: versionB,
        versionB: versionA,
        options: {
          suppressFormatError: true,
          requireOrdering: true
        }
      })
    );
    currentReleaseIndex = sortedVersions.indexOf(currentRelease ?? '');
    versionsAboveCurrent = sortedVersions;
  }
  finalOptions = [
    ...finalOptions,
    ...versionsAboveCurrent.map((e: any) => ({
      version: e,
      info: releases[e],
      series: `v${e.split('.')[0]}.${e.split('.')[1]} Series ${
        isVersionStable(e) ? '(Stable)' : '(Preview)'
      }`
    }))
  ];

  const formMethods = useForm<DBUpgradeFormFields>({
    defaultValues: {
      softwareVersion: '',
      rollingUpgrade: true,
      timeDelay: 180,
      numNodesToUpgradePrimary: 1
    },
    mode: 'onChange',
    reValidateMode: 'onChange'
  });
  const dispatch = useDispatch();
  const { control, watch, handleSubmit, setValue } = formMethods;

  //Upgrade Software
  const upgradeSoftware = useMutation(
    (values: DBUpgradePayload) => {
      return api.upgradeSoftware(universeUUID, values);
    },
    {
      onSuccess: () => {
        toast.success('Database upgrade initiated', TOAST_OPTIONS);
        dispatch(fetchCustomerTasks() as any).then((response: any) => {
          if (!response.error) {
            dispatch(fetchCustomerTasksSuccess(response.payload));
          } else {
            dispatch(fetchCustomerTasksFailure(response.payload));
          }
        });
        //Universe upgrade state is not updating immediately
        setTimeout(() => {
          dispatch(fetchUniverseInfo(universeUUID) as any).then((response: any) => {
            dispatch(fetchUniverseInfoResponse(response.payload));
          });
        }, 2000);
        transitToUniverse(universeUUID);
        onClose();
      },
      onError: (error) => {
        toast.error(createErrorMessage(error), TOAST_OPTIONS);
      }
    }
  );

  const callPrefinalizeCheck = async (version: string) => {
    try {
      const { finalizeRequired } = await api.getUpgradeDetails(universeUUID, {
        ybSoftwareVersion: version
      });
      setPrefinalize(finalizeRequired ? true : false);
    } catch (e) {
      console.error(e);
    }
  };

  const handleFormSubmit = handleSubmit(async (values) => {
    if (universeDetails?.clusters && universeDetails?.nodePrefix) {
      const payload: DBUpgradePayload = {
        ybSoftwareVersion: values?.softwareVersion ? values.softwareVersion : '',
        sleepAfterMasterRestartMillis: Number(values.timeDelay) * 1000,
        sleepAfterTServerRestartMillis: Number(values.timeDelay) * 1000,
        upgradeOption: values.rollingUpgrade ? UPGRADE_TYPE.ROLLING : UPGRADE_TYPE.NON_ROLLING,
        universeUUID,
        taskType: 'Software',
        clusters: universeDetails.clusters,
        nodePrefix: universeDetails.nodePrefix,
        enableYbc: featureFlags.released.enableYbc || featureFlags.test.enableYbc
      };
      if (isRollingUpradeMutlipleNodesEnabled && values.rollingUpgrade) {
        payload.rollMaxBatchSize = {
          primaryBatchSize: values.numNodesToUpgradePrimary,
          readReplicaBatchSize: values.numNodesToUpgradePrimary
        };
      }
      try {
        await upgradeSoftware.mutateAsync(payload);
      } catch (e) {
        console.error(e);
      }
    }
  });

  const ybSoftwareVersionValue = watch('softwareVersion');
  const isRollingUpgradeValue = watch('rollingUpgrade');

  useUpdateEffect(() => {
    if (ybSoftwareVersionValue) {
      callPrefinalizeCheck(ybSoftwareVersionValue);
    }
  }, [ybSoftwareVersionValue]);

  const handleVersionChange = (e: ChangeEvent<{}>, option: any) => {
    setPrefinalize(false);
    setValue('softwareVersion', option?.version, { shouldValidate: true });
  };

  const canUpgradeSoftware = hasNecessaryPerm({
    onResource: universeUUID,
    ...ApiPermissionMap.UPGRADE_NEW_UNIVERSE_SOFTWARE
  });

  const renderDropdown = () => {
    return (
      <Controller
        name={'softwareVersion'}
        control={control}
        rules={{
          required: t('common.fieldRequired')
        }}
        render={({ field: { value, ref }, fieldState }) => {
          const val = finalOptions.find((r) => r.version === value);
          return (
            <YBAutoComplete
              value={(val as unknown) as string[]}
              options={(finalOptions as unknown[]) as Record<string, any>[]}
              groupBy={(option: Record<string, string>) => option.series}
              getOptionLabel={(option: Record<string, string>): string => option.version}
              getOptionDisabled={(option: Record<string, string>): boolean =>
                option.version === currentRelease
              }
              renderOption={renderOption}
              onChange={handleVersionChange}
              ybInputProps={{
                error: !!fieldState.error,
                helperText: fieldState.error?.message,
                'data-testid': 'SoftwareVersion-AutoComplete',
                placeholder: 'Please select',
                id: 'dBVersion'
              }}
            />
          );
        }}
      />
    );
  };

  if (isLoading) {
    return <YBLoadingCircleIcon />;
  }

  const handleNumNodeChangePrimary = (e: FocusEvent<HTMLInputElement>) => {
    const fieldValue = (e.target.value as unknown) as number;
    if (fieldValue > rollMaxBatchSize?.primaryBatchSize)
      setValue('numNodesToUpgradePrimary', rollMaxBatchSize?.primaryBatchSize);
    else if (fieldValue < 1) setValue('numNodesToUpgradePrimary', 1);
    else setValue('numNodesToUpgradePrimary', fieldValue);
  };

  return (
    <YBModal
      open={open}
      titleSeparator
      size="md"
      overrideHeight={universeHasXcluster ? '810px' : '720px'}
      overrideWidth="800px"
      onClose={onClose}
      onSubmit={handleFormSubmit}
      cancelLabel={t('common.cancel')}
      submitLabel={t('universeActions.dbRollbackUpgrade.upgradeAction')}
      title={t('universeActions.dbRollbackUpgrade.modalTitle')}
      submitTestId="DBUpgradeModal-UpgradeButton"
      cancelTestId="DBUpgradeModal-Cancel"
      titleIcon={<UpgradeArrow />}
      buttonProps={{
        primary: {
          disabled: !canUpgradeSoftware
        }
      }}
      submitButtonTooltip={!canUpgradeSoftware ? RBAC_ERR_MSG_NO_PERM : ''}
    >
      <FormProvider {...formMethods}>
        <Box className={classes.mainContainer} data-testid="DBUpgradeModal-Container">
          <Box width="100%" display="flex" flexDirection="column">
            <Box display={'flex'} flexDirection={'column'} width="100%">
              <Typography variant="body1">
                {t('universeActions.dbRollbackUpgrade.chooseTarget')}
              </Typography>
              <Box mt={3} ml={2.5} display={'flex'} flexDirection={'row'} width="100%">
                <Box width="100px">
                  <Typography variant="subtitle1">
                    {t('universeActions.dbRollbackUpgrade.currentVersion')}
                  </Typography>
                </Box>
                <Box ml={4}>
                  <Typography variant="body2">{currentRelease}</Typography>
                </Box>
              </Box>
              <Box
                mt={1}
                ml={2.5}
                display={'flex'}
                flexDirection={'row'}
                width="100%"
                alignItems={'center'}
              >
                <Box width="100px">
                  <Typography variant="subtitle1">
                    {t('universeActions.dbRollbackUpgrade.targetVersion')}
                  </Typography>
                </Box>
                <Box ml={4} width="420px">
                  {renderDropdown()}
                </Box>
              </Box>
              {needPrefinalize && (
                <Box ml={18.5} display="flex" flexDirection={'column'} width="420px">
                  <Box
                    display={'flex'}
                    flexDirection={'row'}
                    alignItems={'center'}
                    width="100%"
                    mt={2}
                  >
                    <img src={ExclamationIcon} alt="--" height={'14px'} width="14px" /> &nbsp;
                    <Typography variant="subtitle2">
                      {t('universeActions.dbRollbackUpgrade.additionalStep')}
                    </Typography>
                  </Box>
                  <Box className={classes.additionalStepContainer}>
                    <Typography variant="subtitle1">
                      {t('universeActions.dbRollbackUpgrade.additionalStepInfo')}
                    </Typography>
                  </Box>
                </Box>
              )}
            </Box>
            <Box mt={6} display={'flex'} flexDirection={'column'} width="100%">
              <Typography variant="body1">
                {t('universeActions.dbRollbackUpgrade.configureUpgrade')}
              </Typography>
              <Box className={classes.upgradeDetailsContainer}>
                <YBCheckboxField
                  name={'rollingUpgrade'}
                  label={t('universeActions.dbRollbackUpgrade.rolllingUpgradeLabel')}
                  control={control}
                  style={{ width: 'fit-content' }}
                  inputProps={{
                    'data-testid': 'DBUpgradeModal-RollingUpgrade'
                  }}
                />
                {isRollingUpgradeValue && rollMaxBatchSize?.primaryBatchSize > 1 && (
                  <Box
                    display={'flex'}
                    flexDirection={'row'}
                    mt={1}
                    ml={1}
                    width="100%"
                    alignItems={'center'}
                  >
                    <YBLabel width="280px">
                      {t('universeActions.dbRollbackUpgrade.numNodesToRollingUpgrade')}
                    </YBLabel>
                    <Box width="130px" mr={1}>
                      <YBInputField
                        control={control}
                        type="number"
                        name="numNodesToUpgradePrimary"
                        fullWidth
                        inputProps={{
                          min: 1,
                          max: rollMaxBatchSize?.primaryBatchSize,
                          autoFocus: true,
                          'data-testid': 'DBUpgradeModal-NumNodesToRollingUpgrade'
                        }}
                        onChange={handleNumNodeChangePrimary}
                      />
                    </Box>
                    <Tooltip
                      title={t('universeActions.dbRollbackUpgrade.rollingUpgradeMsg')}
                      arrow
                      placement="top"
                    >
                      <img src={InfoMessageIcon} alt="info" />
                    </Tooltip>
                  </Box>
                )}
                <Box
                  display={'flex'}
                  flexDirection={'row'}
                  mt={1}
                  ml={1}
                  width="100%"
                  alignItems={'center'}
                >
                  <YBLabel width="280px">
                    {t('universeActions.dbRollbackUpgrade.upgradeDelayLabel')}
                  </YBLabel>
                  <Box width="160px" mr={1}>
                    <YBInputField
                      control={control}
                      type="number"
                      name="timeDelay"
                      fullWidth
                      disabled={!isRollingUpgradeValue}
                      inputProps={{
                        autoFocus: true,
                        'data-testid': 'DBUpgradeModal-TimeDelay'
                      }}
                    />
                  </Box>
                  <Typography variant="body2">{t('common.seconds')}</Typography>
                </Box>
              </Box>
            </Box>
          </Box>
          <Box width="100%" display="flex" flexDirection="column">
            {compareYBSoftwareVersions({
              versionA: currentRelease,
              versionB: MINIMUM_SUPPORTED_VERSION,
              options: {
                suppressFormatError: true
              }
            }) >= 0 && (
              <Box className={classes.greyFooter}>
                <img src={BulbIcon} alt="--" height={'32px'} width={'32px'} />
                <Box ml={0.5} mt={0.5}>
                  <Typography variant="body2">
                    {t('universeActions.dbRollbackUpgrade.footerMsg1')}
                    <b>{t('universeActions.dbRollbackUpgrade.rollbackPrevious')}</b>&nbsp;
                    {t('universeActions.dbRollbackUpgrade.footerMsg2')}
                  </Typography>
                </Box>
              </Box>
            )}
            {universeHasXcluster && (
              <Box className={classes.xclusterBanner}>
                <Box display="flex" mr={1}>
                  <img src={WarningIcon} alt="---" height={'22px'} width="22px" />
                </Box>
                <Box display="flex" flexDirection={'column'} mt={0.5} width="100%">
                  <Typography variant="body1">
                    {t('universeActions.dbRollbackUpgrade.avoidDisruption')}
                  </Typography>
                  <Box display="flex" mt={1.5}>
                    <Typography variant="body2">
                      {t('universeActions.dbRollbackUpgrade.xclusterWarning')}
                    </Typography>
                  </Box>
                </Box>
              </Box>
            )}
          </Box>
        </Box>
      </FormProvider>
    </YBModal>
  );
};
