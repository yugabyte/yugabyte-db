/*
 * Created on Thu Nov 16 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

/**
 * Utility functions and components related to Linux version catalog and image bundles.
 *
 * This file contains utility functions and components used in the Linux version catalog and image bundle management.
 * It includes functions for retrieving and manipulating image bundles, as well as components for displaying default tags and active tags.
 *
 */
import { find, has } from 'lodash';
import { useTranslation } from 'react-i18next';
import { FormHelperText, Tooltip, Typography, makeStyles } from '@material-ui/core';
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

/**
 * The key for the runtime configuration entry that enables VM OS patching.
 */
export const VM_PATCHING_RUNTIME_CONFIG = 'yb.provider.vm_os_patching';

/**
 * Constant representing the configuration key for allowing editing of a bundle that is currently in use.
 */
export const VM_ALLOW_EDIT_BUNDLE_IN_USE = 'yb.edit_provider.new.allow_used_bundle_edit';

/**
 * Default styles for the ImageBundleDefaultTag component.
 */
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

/**
 * A component that displays a default tag for an image bundle.
 *
 * @param text - The text to display in the tag.
 * @param tooltip - The tooltip text to display when hovering over the tag.
 * @param icon - The icon to display in the tag.
 * @returns The ImageBundleDefaultTag component.
 */
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

/**
 * Default styles for the ImageBundleYBActiveTag component.
 */
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

/**
 * A component that displays an active tag for a YB image bundle.
 *
 * @returns The ImageBundleYBActiveTag component.
 */
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

/**
 * A sample x86 image bundle.
 */
export const sampleX86Image: Partial<ImageBundle> = {
  name: 'YBA-Managed-x86',
  details: {
    arch: ArchitectureType.X86_64,
    regions: {},
    sshPort: 22
  },
  metadata: {
    type: ImageBundleType.YBA_ACTIVE
  }
} as any;

/**
 * A sample ARM image bundle.
 */
export const sampleAarchImage: Partial<ImageBundle> = {
  name: 'YBA-Managed-aarch',
  details: {
    arch: ArchitectureType.ARM64,
    regions: {},
    sshPort: 22
  },
  metadata: {
    type: ImageBundleType.YBA_ACTIVE
  }
} as any;

/**
 * Retrieves the image bundle used by the given universe.
 *
 * @param universeDetails - The details of the universe.
 * @param providers - The list of providers.
 * @returns The image bundle used by the universe, or null if not found.
 */
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

/**
 * Constructs the payload for image bundles based on the form values.
 *
 * @param formValues - The form values.
 * @param isAWS - Whether the provider is AWS.
 * @returns The constructed image bundle payload.
 */
export const constructImageBundlePayload = (formValues: any, isAWS = false) => {
  const imageBundles = [...formValues.imageBundles];

  imageBundles.forEach((img) => {
    formValues.regions.forEach((region: CloudVendorRegionField) => {
      // Only AWS supports region specific AMI
      if (isAWS && !has(img.details.regions, region.code)) {
        img.details.regions[region.code] = {};
      }
    });
  });
  return imageBundles;
};

/**
 * Checks if VM OS patching is enabled.
 *
 * @returns Whether VM OS patching is enabled.
 */
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

/**
 * Checks if the image bundle is supported by the provider.
 *
 * @param provider - The provider.
 * @returns Whether the image bundle is supported by the provider.
 */
export const isImgBundleSupportedByProvider = (provider: Provider) =>
  [CloudType.aws, CloudType.azu, CloudType.gcp].includes(provider?.code);

/**
 * Displays a message for configuring SSH details.
 *
 * @returns The ConfigureSSHDetailsMsg component.
 */
export function ConfigureSSHDetailsMsg() {
  const { t } = useTranslation();
  if (!IsOsPatchingEnabled()) return null;
  return <FormHelperText error={true}>{t('linuxVersion.sshOverrideMsg')}</FormHelperText>;
}

/**
 * Checks if the Image bundle is in-use edit enabled.
 *
 * @returns Whether the provider is in-use edit enabled.
 */
export function IsImgBundleInUseEditEnabled() {
  const {
    data: globalRuntimeConfigs,
    isLoading: isRuntimeConfigLoading
  } = useQuery(runtimeConfigQueryKey.globalScope(), () =>
    fetchGlobalRunTimeConfigs(true).then((res: any) => res.data)
  );

  const isImgBundleInUseEditEnabled =
    globalRuntimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === VM_ALLOW_EDIT_BUNDLE_IN_USE
    )?.value === 'true';

  if (isRuntimeConfigLoading) return false;
  return isImgBundleInUseEditEnabled;
}
