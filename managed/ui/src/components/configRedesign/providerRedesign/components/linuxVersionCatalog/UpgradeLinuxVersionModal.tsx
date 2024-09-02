/*
 * Created on Fri Nov 24 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import clsx from 'clsx';
import { useForm } from 'react-hook-form';
import { toast } from 'react-toastify';
import { Trans, useTranslation } from 'react-i18next';
import { useSelector } from 'react-redux';
import { find, isEmpty } from 'lodash';
import * as yup from 'yup';
import { useMutation } from 'react-query';
import { Grid, MenuItem, Typography, makeStyles } from '@material-ui/core';
import { AlertVariant, YBAlert, YBInputField, YBModal, YBSelectField } from '../../../../../redesign/components';
import { createErrorMessage } from '../../../../../redesign/features/universe/universe-form/utils/helpers';
import { ClusterType, Universe } from '../../../../../redesign/helpers/dtos';
import {
  ImageBundle,
  ImageBundleType
} from '../../../../../redesign/features/universe/universe-form/utils/dto';
import { ImageBundleDefaultTag, ImageBundleYBActiveTag } from './LinuxVersionUtils';
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
    marginTop: '50px'
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
  }
}));

export const UpgradeLinuxVersionModal: FC<UpgradeLinuxVersionModalProps> = ({
  visible,
  onHide,
  universeData
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'linuxVersion.upgradeModal'
  });

  const allProviders = useSelector((data: any) => data.cloud.providers) ?? [];

  const {
    control,
    handleSubmit,
    setError,
    formState: { errors }
  } = useForm<UpgradeLinuxVersionModalForm>({
    defaultValues: {
      sleepAfterInSeconds: 180,
      targetVersion: null
    }
  });

  const classes = useStyles();

  const doUpgradeVM = useMutation(
    ({
      imageBundleUUID,
      nodePrefix,
      sleepAfterInSeconds
    }: {
      imageBundleUUID: string;
      nodePrefix: string;
      sleepAfterInSeconds: number;
    }) => {
      return upgradeVM(universeData.universeUUID, {
        taskType: 'VMImage',
        upgradeOption: 'Rolling',
        imageBundleUUID,
        nodePrefix,
        sleepAfterMasterRestartMillis: sleepAfterInSeconds * 1000,
        sleepAfterTServerRestartMillis: sleepAfterInSeconds * 1000,
        clusters: universeData?.universeDetails.clusters
      });
    },
    {
      onSuccess: (resp) => {
        const taskUUID = resp.data.taskUUID;
        toast.success(
          <span>
            <Trans
              i18nKey="linuxVersion.upgradeModal.successMsg"
              components={[
                <a href={`/tasks/${taskUUID}`} target="_blank" rel="noopener noreferrer">
                  here
                </a>
              ]}
            ></Trans>
          </span>
        );
        onHide();
      },
      onError: (resp: any) => {
        const errMsg = createErrorMessage(resp);
        toast.error(errMsg);
        onHide();
      }
    }
  );

  if (!visible) return null;

  const clusters = universeData?.universeDetails.clusters;
  if (!clusters) return null;

  const primaryCluster = find(clusters, { clusterType: ClusterType.PRIMARY });

  const currProvider = find(allProviders.data, { uuid: primaryCluster?.userIntent.provider });

  const curLinuxImgBundle: ImageBundle = find(currProvider.imageBundles, {
    uuid: primaryCluster?.userIntent.imageBundleUUID
  });

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
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      submitLabel={t('submitLabel')}
      buttonProps={{
        primary: {
          disabled: !isEmpty(errors)
        }
      }}
      onSubmit={() => {
        handleSubmit(async (values) => {
          try {
            await validationSchema.validate(values);
            doUpgradeVM.mutate({
              imageBundleUUID: values.targetVersion!,
              nodePrefix: universeData.universeDetails.nodePrefix,
              sleepAfterInSeconds: values.sleepAfterInSeconds
            });
          } catch (e) {
            setError('targetVersion', {
              message: (e as yup.ValidationError)?.message
            });
          }
        })();
      }}
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
      <Grid container className={classes.restartInterval}>
        <Grid item xs={3} className={classes.versionComp}>
          <Typography variant="body1">{t('restartAfterInSecond')}</Typography>
        </Grid>
        <Grid item xs={5}>
          <YBInputField control={control} name={'sleepAfterInSeconds'} type="number" fullWidth />
        </Grid>
      </Grid>
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
      <div className={classes.info}>
        <Trans
          i18nKey={`linuxVersion.upgradeModal.infoContent`}
          components={{
            b: <b />,
            br: <br />
          }}
        />
      </div>
    </YBModal>
  );
};
