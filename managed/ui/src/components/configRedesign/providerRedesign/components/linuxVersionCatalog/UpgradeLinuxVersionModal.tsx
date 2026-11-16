/*
 * Created on Fri Nov 24 2023
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';

import { Grid, MenuItem, Typography, makeStyles } from '@material-ui/core';
import clsx from 'clsx';
import { find, isEmpty } from 'lodash';
import { useForm } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { useMutation, useQuery } from 'react-query';
import { useDispatch, useSelector } from 'react-redux';
import { toast } from 'react-toastify';
import * as yup from 'yup';

import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import { useEditUniverse } from '@app/v2/api/universe/universe';
import { showTaskInDrawer } from '../../../../../actions/tasks';
import { ProviderCode } from '../../constants';
import {
  AlertVariant,
  YBAlert,
  YBButton,
  YBInputField,
  YBModal,
  YBSelectField
} from '../../../../../redesign/components';
import {
  useInterceptBackupTaskLinks,
  useIsTaskNewUIEnabled
} from '../../../../../redesign/features/tasks/TaskUtils';
import { UPGRADE_TYPE } from '../../../../../redesign/features/universe/universe-actions/rollback-upgrade/utils/types';
import {
  ImageBundle,
  ImageBundleType,
  PRECHECK_UPGRADE_TYPE
} from '../../../../../redesign/features/universe/universe-form/utils/dto';
import {
  createErrorMessage,
  transitToUniverse
} from '../../../../../redesign/features/universe/universe-form/utils/helpers';
import { api, runtimeConfigQueryKey } from '../../../../../redesign/helpers/api';
import { RuntimeConfigKey } from '../../../../../redesign/helpers/constants';
import { ClusterType, Universe } from '../../../../../redesign/helpers/dtos';
import { ImageBundleDefaultTag, ImageBundleYBActiveTag } from './LinuxVersionUtils';
import {
  OciImageUpgradeMode,
  getOciImageBundleUpgradeMode,
  type OciBundleRegionCheck
} from './ociImageBundleUpgrade';
import { upgradeVM } from './VersionCatalogApi';

interface UpgradeLinuxVersionModalProps {
  visible: boolean;
  onHide: () => void;
  universeData: Universe;
}

interface UpgradeLinuxVersionModalForm {
  targetVersion: null;
  sleepAfterInSeconds: number;
}

const useStyles = makeStyles((theme) => ({
  icon: {
    color: theme.palette.ybacolors.ybIcon
  },
  content: {
    padding: '46px 40px'
  },
  versionComp: {
    display: 'flex',
    gap: '8px',
    alignItems: 'center'
  },
  targetVersion: {
    marginTop: '28px'
  },
  alert: {
    marginTop: '16px'
  },
  info: {
    marginTop: '16px',
    padding: '15px',
    borderRadius: '4px',
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    background: 'rgba(240, 244, 247, 0.50)',
    fontSize: '14px'
  },
  restartInterval: {
    marginTop: '28px'
  },
  loadingContainer: {
    /* Layout */
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',

    /* Box Model */
    height: '100%',
    width: '100%'
  }
}));

export const UpgradeLinuxVersionModal: FC<UpgradeLinuxVersionModalProps> = ({
  visible,
  onHide,
  universeData
}) => {
  const dispatch = useDispatch();

  const { t } = useTranslation('translation', {
    keyPrefix: 'linuxVersion.upgradeModal'
  });

  const isNewTaskUIEnabled = useIsTaskNewUIEnabled();
  const interceptBackupLink = useInterceptBackupTaskLinks();

  const allProviders = useSelector((data: any) => data.cloud.providers) ?? [];

  const {
    control,
    handleSubmit,
    setError,
    watch,
    formState: { errors }
  } = useForm<UpgradeLinuxVersionModalForm>({
    defaultValues: {
      sleepAfterInSeconds: 180,
      targetVersion: null
    }
  });

  const targetVersionValue = watch('targetVersion');

  const classes = useStyles();

  const universeRuntimeConfigsQuery = useQuery(
    runtimeConfigQueryKey.universeScope(universeData.universeUUID),
    () => api.fetchRuntimeConfigs(universeData.universeUUID),
    { enabled: visible && !!universeData.universeUUID }
  );
  const allowInPlaceForMarketplaceBasedImage =
    universeRuntimeConfigsQuery.data?.configEntries?.find(
      (configEntry: { key: string; value: string }) =>
        configEntry.key === RuntimeConfigKey.OCI_ALLOW_INPLACE_MARKETPLACE_BASED_IMAGE_UPGRADE
    )?.value === 'true';

  const clusters = universeData?.universeDetails.clusters;
  const primaryCluster = find(clusters, { clusterType: ClusterType.PRIMARY });
  const currProvider = find(allProviders.data, { uuid: primaryCluster?.userIntent.provider });
  const curLinuxImgBundle: ImageBundle | undefined = currProvider
    ? find(currProvider.imageBundles, {
        uuid: primaryCluster?.userIntent.imageBundleUUID
      })
    : undefined;
  const regionCodesByCluster: Record<string, string[]> = {};
  (universeData?.universeDetails.nodeDetailsSet ?? []).forEach((node) => {
    const regionCode = node.cloudInfo?.region;
    if (!regionCode) {
      return;
    }
    const clusterRegionCodes = regionCodesByCluster[node.placementUuid] ?? [];
    if (!clusterRegionCodes.includes(regionCode)) {
      clusterRegionCodes.push(regionCode);
    }
    regionCodesByCluster[node.placementUuid] = clusterRegionCodes;
  });
  const currentBundleChecks: OciBundleRegionCheck[] = (clusters ?? []).map((cluster) => {
    const clusterProvider = find(allProviders.data, { uuid: cluster.userIntent.provider });
    return {
      bundle: clusterProvider
        ? find(clusterProvider.imageBundles, { uuid: cluster.userIntent.imageBundleUUID })
        : undefined,
      regionCodes: regionCodesByCluster[cluster.uuid] ?? []
    };
  });
  const allRegionCodes = [...new Set(currentBundleChecks.flatMap((check) => check.regionCodes))];
  const upgradeModeForTarget = (targetUuid: string | null) => {
    if (currProvider?.code !== ProviderCode.OCI) {
      return OciImageUpgradeMode.IN_PLACE;
    }
    const selectedBundle: ImageBundle | undefined = targetUuid
      ? find(currProvider?.imageBundles, { uuid: targetUuid })
      : undefined;
    const checks = targetUuid
      ? [...currentBundleChecks, { bundle: selectedBundle, regionCodes: allRegionCodes }]
      : currentBundleChecks;
    return getOciImageBundleUpgradeMode(checks);
  };
  const upgradeMode = upgradeModeForTarget(targetVersionValue);
  const isFullMove =
    upgradeMode === OciImageUpgradeMode.MARKETPLACE_BASED && !allowInPlaceForMarketplaceBasedImage;

  const showUpgradeStarted = (taskUUID: string | undefined, runOnlyPrechecks: boolean) => {
    toast.success(
      <span>
        {runOnlyPrechecks ? (
          t('precheckInitiatedMsg', { keyPrefix: 'universeActions' })
        ) : (
          <Trans
            i18nKey="linuxVersion.upgradeModal.successMsg"
            components={[
              interceptBackupLink(
                <a href={`/tasks/${taskUUID}`} target="_blank" rel="noopener noreferrer">
                  here
                </a>
              )
            ]}
          ></Trans>
        )}
      </span>
    );
    if (isNewTaskUIEnabled && taskUUID) {
      dispatch(showTaskInDrawer(taskUUID));
    } else {
      transitToUniverse(universeData.universeUUID);
    }
    onHide();
  };

  const doUpgradeVM = useMutation(
    ({
      imageBundleUUID,
      nodePrefix,
      sleepAfterInSeconds,
      runOnlyPrechecks
    }: {
      imageBundleUUID: string;
      nodePrefix: string;
      sleepAfterInSeconds: number;
      runOnlyPrechecks: boolean;
    }) => {
      return upgradeVM(universeData.universeUUID, {
        taskType: PRECHECK_UPGRADE_TYPE.VMIMAGE,
        upgradeOption: UPGRADE_TYPE.ROLLING,
        imageBundleUUID,
        nodePrefix,
        sleepAfterMasterRestartMillis: sleepAfterInSeconds * 1000,
        sleepAfterTServerRestartMillis: sleepAfterInSeconds * 1000,
        clusters: universeData?.universeDetails.clusters,
        runOnlyPrechecks
      });
    },
    {
      onSuccess: (resp, { runOnlyPrechecks }) => {
        showUpgradeStarted(resp.data.taskUUID, runOnlyPrechecks);
      },
      onError: (resp: any) => {
        const errMsg = createErrorMessage(resp);
        toast.error(errMsg);
        onHide();
      }
    }
  );

  const editUniverseMutation = useEditUniverse({
    mutation: {
      onSuccess: (response) => {
        showUpgradeStarted(response.task_uuid, false);
      },
      onError: (error: unknown) => {
        toast.error(createErrorMessage(error));
        onHide();
      }
    }
  });

  if (!visible || !clusters || !currProvider || !curLinuxImgBundle) return null;

  if (universeRuntimeConfigsQuery.isLoading) {
    return (
      <YBModal
        open={visible}
        onClose={onHide}
        title={t('title')}
        titleIcon={<i className={clsx('fa fa-arrow-up fa-fw', classes.icon)} />}
        dialogContentProps={{
          dividers: true,
          className: classes.content
        }}
        overrideWidth={'886px'}
        overrideHeight={'720px'}
        footerAccessory={
          <div
            style={{
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'flex-end',
              gap: '8px',
              width: '100%'
            }}
          >
            <YBButton
              type="button"
              variant="secondary"
              data-testid="UpgradeLinuxVersionModal-CancelButton"
              onClick={onHide}
            >
              {t('cancel', { keyPrefix: 'common' })}
            </YBButton>
          </div>
        }
      >
        <div className={classes.loadingContainer}>
          <YBLoadingCircleIcon />
        </div>
      </YBModal>
    );
  }

  const validationSchema = yup.object({
    targetVersion: yup
      .string()
      .nullable(true)
      .required(t('requiredField', { keyPrefix: 'common' }))
      .notOneOf(
        [curLinuxImgBundle?.uuid],
        t('sameVersionErrMsg', { image_name: curLinuxImgBundle?.name })
      )
  });

  const handleFormSubmit = (runOnlyPrechecks = false) =>
    handleSubmit(async (values) => {
      try {
        await validationSchema.validate(values);
        if (isFullMove) {
          editUniverseMutation.mutate({
            uniUUID: universeData.universeUUID,
            data: {
              expected_universe_version: -1,
              clusters: clusters.map((cluster) => ({
                uuid: cluster.uuid,
                provider_spec: {
                  region_list: cluster.userIntent.regionList,
                  image_bundle_uuid: values.targetVersion!
                }
              }))
            }
          });
          return;
        }
        doUpgradeVM.mutateAsync({
          imageBundleUUID: values.targetVersion!,
          nodePrefix: universeData.universeDetails.nodePrefix,
          sleepAfterInSeconds: values.sleepAfterInSeconds,
          runOnlyPrechecks
        });
      } catch (e) {
        setError('targetVersion', {
          message: (e as yup.ValidationError)?.message
        });
      }
    });

  return (
    <YBModal
      open={visible}
      onClose={onHide}
      title={t('title')}
      titleIcon={<i className={clsx('fa fa-arrow-up fa-fw', classes.icon)} />}
      dialogContentProps={{
        dividers: true,
        className: classes.content
      }}
      overrideWidth={'886px'}
      overrideHeight={'720px'}
      footerAccessory={
        <div
          style={{
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'flex-end',
            gap: '8px',
            width: '100%'
          }}
        >
          <YBButton
            type="button"
            variant="secondary"
            data-testid="UpgradeLinuxVersionModal-CancelButton"
            onClick={onHide}
          >
            {t('cancel', { keyPrefix: 'common' })}
          </YBButton>
          {!isFullMove && (
            <YBButton
              type="button"
              variant="secondary"
              disabled={!targetVersionValue}
              onClick={handleFormSubmit(true)}
              data-testid="UpgradeLinuxVersionModal-PrecheckButton"
            >
              {t('runPrecheckOnlyButton')}
            </YBButton>
          )}
          <YBButton
            variant="primary"
            disabled={!isEmpty(errors) || !targetVersionValue}
            onClick={handleFormSubmit()}
            data-testid="UpgradeLinuxVersionModal-UpgradeButton"
          >
            {t('submitLabel')}
          </YBButton>
        </div>
      }
    >
      <Grid container>
        <Grid item xs={3} className={classes.versionComp}>
          <Typography variant="body1">{t('currentVersion')}</Typography>
        </Grid>
        <Grid item xs={9} className={classes.versionComp}>
          {curLinuxImgBundle?.name}
          {curLinuxImgBundle?.metadata?.type === ImageBundleType.YBA_ACTIVE && (
            <ImageBundleYBActiveTag />
          )}
          {curLinuxImgBundle?.metadata?.type === ImageBundleType.YBA_DEPRECATED && (
            <ImageBundleDefaultTag text={t('retired')} icon={<></>} tooltip={t('retiredTooltip')} />
          )}
        </Grid>
      </Grid>
      <Grid container className={classes.targetVersion}>
        <Grid item xs={3} className={classes.versionComp}>
          <Typography variant="body1">{t('targetVersion')}</Typography>
        </Grid>
        <Grid item xs={5} className={classes.versionComp}>
          <YBSelectField
            control={control}
            name="targetVersion"
            renderValue={(selectedVal) => {
              if (!selectedVal) return null;
              const img: ImageBundle = find(currProvider.imageBundles, { uuid: selectedVal });
              return (
                <div className={classes.versionComp}>
                  {img.name}
                  {img.metadata?.type === ImageBundleType.YBA_ACTIVE && <ImageBundleYBActiveTag />}
                  {img.useAsDefault && <ImageBundleDefaultTag />}
                </div>
              );
            }}
            fullWidth
          >
            {currProvider.imageBundles
              .filter(
                (img: ImageBundle) =>
                  img.details.arch === curLinuxImgBundle.details.arch &&
                  img?.uuid !== curLinuxImgBundle?.uuid
              )
              .map((img: ImageBundle) => (
                <MenuItem key={img.uuid} value={img.uuid} className={classes.versionComp}>
                  {img.name}
                  {img.metadata?.type === ImageBundleType.YBA_ACTIVE && <ImageBundleYBActiveTag />}
                  {img.useAsDefault && <ImageBundleDefaultTag />}
                </MenuItem>
              ))}
          </YBSelectField>
        </Grid>
      </Grid>
      {isFullMove && (
        <YBAlert
          open
          text={t('marketplaceBasedWarning', {
            configKey: RuntimeConfigKey.OCI_ALLOW_INPLACE_MARKETPLACE_BASED_IMAGE_UPGRADE
          })}
          variant={AlertVariant.Warning}
          className={classes.alert}
        />
      )}
      {upgradeMode === OciImageUpgradeMode.UNKNOWN && (
        <YBAlert
          open
          text={t('unknownMarketplaceFlagWarning')}
          variant={AlertVariant.Warning}
          className={classes.alert}
        />
      )}
      {!isFullMove && (
        <Grid container className={classes.restartInterval}>
          <Grid item xs={3} className={classes.versionComp}>
            <Typography variant="body1">{t('restartAfterInSecond')}</Typography>
          </Grid>
          <Grid item xs={5}>
            <YBInputField control={control} name={'sleepAfterInSeconds'} type="number" fullWidth />
          </Grid>
        </Grid>
      )}
      {!isFullMove && (
        <YBAlert
          open
          text={
            <Trans
              i18nKey={`linuxVersion.upgradeModal.verifyImageText`}
              components={{
                b: <b />
              }}
            />
          }
          variant={AlertVariant.Warning}
          className={classes.alert}
        />
      )}
      <div className={classes.info}>
        <Trans
          i18nKey={`linuxVersion.upgradeModal.${
            isFullMove ? 'fullMoveInfoContent' : 'infoContent'
          }`}
          components={{
            b: <b />,
            br: <br />
          }}
        />
      </div>
    </YBModal>
  );
};
