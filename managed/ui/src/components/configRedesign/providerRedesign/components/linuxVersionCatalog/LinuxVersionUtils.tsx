/*
 * Created on Thu Nov 16 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { find, has } from 'lodash';
import { useTranslation } from 'react-i18next';
import { Tooltip, Typography, makeStyles } from '@material-ui/core';
import { useQuery } from 'react-query';
import {
  CloudType,
  ImageBundle,
  ImageBundleType,
  Provider,
  RunTimeConfigEntry
} from '../../../../../redesign/features/universe/universe-form/utils/dto';
import { ArchitectureType } from '../../constants';
import { ClusterType, UniverseDetails } from '../../../../../redesign/helpers/dtos';
import { runtimeConfigQueryKey } from '../../../../../redesign/helpers/api';
import { CloudVendorRegionField } from '../../forms/configureRegion/ConfigureRegionModal';
import { fetchGlobalRunTimeConfigs } from '../../../../../api/admin';
import FlagIcon from '../../../../../redesign/assets/flag-secondary.svg';
import YBLogo from '../../../../../redesign/assets/yb-logo-secondary.svg';

export const VM_PATCHING_RUNTIME_CONFIG = 'yb.provider.vm_os_patching';

const defaultTagStyle = makeStyles((theme) => ({
  root: {
    height: '24px',
    padding: '2px 6px',
    display: 'flex',
    gap: '4px',
    background: theme.palette.grey[200],
    borderRadius: '4px',
    alignItems: 'center',
    justifyContent: 'center'
  }
}));

export const ImageBundleDefaultTag = ({
  text,
  tooltip,
  icon
}: {
  text?: string;
  tooltip?: string;
  icon?: React.ReactChild;
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'common' });
  const classes = defaultTagStyle();

  return (
    <Tooltip
      title={
        <Typography variant="subtitle1">
          {tooltip ? tooltip : t('form.menuActions.defaultVersion', { keyPrefix: 'linuxVersion' })}
        </Typography>
      }
      arrow
      placement="top"
    >
      <Typography variant="subtitle1" className={classes.root} component={'span'}>
        {icon ? icon : <img alt="default" src={FlagIcon} width="18" />}
        {text ? text : t('default')}
      </Typography>
    </Tooltip>
  );
};

const ImageBundleYBActiveTagStyle = makeStyles((theme) => ({
  root: {
    width: '26px',
    height: '24px',
    padding: '2px 6px',
    background: theme.palette.grey[200],
    borderRadius: '4px',
    alignItems: 'center'
  }
}));
export const ImageBundleYBActiveTag = () => {
  const classes = ImageBundleYBActiveTagStyle();
  const { t } = useTranslation('translation', { keyPrefix: 'linuxVersion.form.menuActions' });
  return (
    <Tooltip
      title={<Typography variant="subtitle1">{t('recommendedVersion')}</Typography>}
      arrow
      placement="top"
    >
      <div className={classes.root}>
        <img alt="yb-active" src={YBLogo} width={14} height={14} />
      </div>
    </Tooltip>
  );
};

export const sampleX86Image: Partial<ImageBundle> = {
  name: 'YBA-Managed-x86',
  details: {
    arch: ArchitectureType.X86_64,
    regions: {}
  },
  metadata: {
    type: ImageBundleType.YBA_ACTIVE
  }
} as any;

export const sampleAarchImage: Partial<ImageBundle> = {
  name: 'YBA-Managed-aarch',
  details: {
    arch: ArchitectureType.ARM64,
    regions: {}
  },
  metadata: {
    type: ImageBundleType.YBA_ACTIVE
  }
} as any;

export const getImageBundleUsedByUniverse = (universeDetails: UniverseDetails, providers: any) => {
  const clusters = universeDetails.clusters;
  if (!clusters) return null;

  const primaryCluster = find(clusters, { clusterType: ClusterType.PRIMARY });

  const currProvider = find(providers, { uuid: primaryCluster?.userIntent.provider });
  if (!currProvider) return null;

  const curLinuxImgBundle: ImageBundle = find(currProvider?.imageBundles, {
    uuid: primaryCluster?.userIntent.imageBundleUUID
  });

  return curLinuxImgBundle ?? null;
};

export const constructImageBundlePayload = (formValues: any, isAWS = false) => {
  const imageBundles = [...formValues.imageBundles];

  imageBundles.forEach((img) => {
    const sshUserOverride = (img as any).sshUserOverride;
    const sshPortOverride = (img as any).sshPortOverride;

    formValues.regions.forEach((region: CloudVendorRegionField) => {
      // Only AWS supports region specific AMI
      if (isAWS && !has(img.details.regions, region.code)) {
        img.details.regions[region.code] = {};
      }

      if (isAWS && sshUserOverride) {
        img.details.regions[region.code]['sshUserOverride'] = sshUserOverride;
      }
      if (isAWS && sshPortOverride) {
        img.details.regions[region.code]['sshPortOverride'] = parseInt(sshPortOverride);
      }
    });
  });
  return imageBundles;
};

export function IsOsPatchingEnabled() {
  const {
    data: globalRuntimeConfigs,
    isLoading: isRuntimeConfigLoading
  } = useQuery(runtimeConfigQueryKey.globalScope(), () =>
    fetchGlobalRunTimeConfigs(true).then((res: any) => res.data)
  );

  const osPatchingEnabled =
    globalRuntimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === VM_PATCHING_RUNTIME_CONFIG
    )?.value === 'true';

  if (isRuntimeConfigLoading) return false;
  return osPatchingEnabled;
}

export const isImgBundleSupportedByProvider = (provider: Provider) =>
  [CloudType.aws, CloudType.azu, CloudType.gcp].includes(provider?.code);
