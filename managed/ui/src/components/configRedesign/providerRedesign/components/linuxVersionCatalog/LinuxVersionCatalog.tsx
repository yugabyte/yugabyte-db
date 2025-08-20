/*
 * Created on Fri Nov 17 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useCallback, useState, useEffect } from 'react';
import { useTranslation, Trans } from 'react-i18next';
import { useToggle } from 'react-use';
import { Control, useFieldArray, useWatch } from 'react-hook-form';
import { makeStyles, Typography, Link } from '@material-ui/core';
import { FieldGroup } from '../../forms/components/FieldGroup';
import { YBCheckbox } from '../../../../../redesign/components';
import { YBTooltip } from '../../../../../redesign/components/YBTooltip/YBTooltip';
import { LinuxVersionsList } from './LinuxVersionsList';
import { AddLinuxVersionModal } from './AddLinuxVersionModal';
import { IsOsPatchingEnabled, sampleAarchImage, sampleX86Image } from './LinuxVersionUtils';
import {
  ImageBundle,
  ImageBundleType
} from '../../../../../redesign/features/universe/universe-form/utils/dto';
import { RbacValidator } from '../../../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../../../redesign/features/rbac/ApiAndUserPermMapping';
import { AWSProviderCreateFormFieldValues } from '../../forms/aws/AWSProviderCreateForm';
import { ArchitectureType, ProviderCode, ProviderOperation, ProviderStatus } from '../../constants';
import { EmptyListPlaceholder } from '../../EmptyListPlaceholder';
import { YBButton } from '../../../../common/forms/fields';
import { UniverseItem } from '../../providerView/providerDetails/UniverseTable';
import { getInUseImageBundleUuids } from '../../utils';

interface LinuxVersionCatalogProps {
  control: Control<AWSProviderCreateFormFieldValues>;
  providerType: ProviderCode;
  isDisabled: boolean;
  providerOperation: ProviderOperation;

  linkedUniverses?: UniverseItem[];
  providerStatus?: ProviderStatus;
}
const useStyles = makeStyles((theme) => ({
  root: {},
  filters: {
    display: 'flex'
  }
}));

type YbImageOptions = {
  useYBImages: boolean;
  useX86: boolean;
  useArm: boolean;
};

export const LinuxVersionCatalog: FC<LinuxVersionCatalogProps> = ({
  control,
  providerType,
  isDisabled,
  providerOperation,
  linkedUniverses
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'linuxVersion'
  });
  const classes = useStyles();
  const [showLinuxVersionModal, toggleShowLinuxVersionModal] = useToggle(false);
  const fieldArray = useFieldArray({
    name: 'imageBundles',
    control
  });
  const { append, replace } = fieldArray;
  const regions = useWatch({ name: 'regions', control });
  const imageBundles = useWatch({ name: 'imageBundles', control }) ?? [];

  const getYBManagedImgBundleByArch = (imgBundle: ImageBundle[], arch: ArchitectureType) =>
    imgBundle.find(
      (i) => i.metadata?.type === ImageBundleType.YBA_ACTIVE && i.details.arch === arch
    );
  const ybManagedX86Bundle = getYBManagedImgBundleByArch(imageBundles, ArchitectureType.X86_64);
  const ybManagedArmBundle = getYBManagedImgBundleByArch(imageBundles, ArchitectureType.ARM64);
  const [ybImageOptions, setUseYBImages] = useState<YbImageOptions>(() => ({
    useArm: !!ybManagedArmBundle,
    useX86: !!ybManagedX86Bundle,
    useYBImages:
      providerType === ProviderCode.AWS
        ? !!ybManagedArmBundle || !!ybManagedX86Bundle
        : !!ybManagedX86Bundle
  }));

  // For edit mode, track which bundles are in use.
  const inUseImageBundleUuids = linkedUniverses
    ? getInUseImageBundleUuids(linkedUniverses)
    : new Set<string>();
  const isYbManagedX86ImageBundleInUse = inUseImageBundleUuids.has(ybManagedX86Bundle?.uuid ?? '');
  const isYbManagedArmImageBundleInUse = inUseImageBundleUuids.has(ybManagedArmBundle?.uuid ?? '');

  const isEditMode = providerOperation === ProviderOperation.EDIT;

  const filterNonYBImages = (images: ImageBundle[]) => {
    return images.filter((i) => i.metadata?.type === ImageBundleType.CUSTOM);
  };

  const updateImageBundles = (ybImageOptions: YbImageOptions, fields: ImageBundle[]) => {
    if (!isEditMode && !ybImageOptions.useYBImages) {
      replace(filterNonYBImages(fields));
      return;
    }

    const images = [...filterNonYBImages(fields)];

    if (ybImageOptions.useX86) {
      if (isEditMode && ybManagedX86Bundle?.active) {
        ybManagedX86Bundle && images.push(ybManagedX86Bundle!);
      } else {
        images.push({
          ...(sampleX86Image as any),
          details: {
            ...sampleX86Image.details,
            regions: Object.assign({}, ...regions.map((r) => ({ [r.code]: {} })))
          },
          useAsDefault:
            images.filter(
              (i) => i.details.arch === ArchitectureType.X86_64 && i.useAsDefault === true
            ).length === 0
        });
      }
    }
    if (ybImageOptions.useArm && providerType === ProviderCode.AWS) {
      if (isEditMode && ybManagedArmBundle?.active) {
        ybManagedArmBundle && images.push(ybManagedArmBundle!);
      } else {
        images.push({
          ...(sampleAarchImage as any),
          details: {
            ...sampleAarchImage.details,
            regions: Object.assign({}, ...regions.map((r) => ({ [r.code]: {} })))
          },
          useAsDefault:
            images.filter(
              (i) => i.details.arch === ArchitectureType.ARM64 && i.useAsDefault === true
            ).length === 0
        });
      }
    }

    replace(images as any);
  };

  const dbNodePublicInternetAccess = useWatch({ name: 'dbNodePublicInternetAccess' });

  useEffect(() => {
    if (!dbNodePublicInternetAccess && ybImageOptions.useYBImages) {
      // If internet access is disabled and YB images are currently selected,
      // automatically uncheck the YB images option
      const newOptions: YbImageOptions = {
        ...ybImageOptions,
        useYBImages: false,
        useX86: false,
        useArm: false
      };
      setUseYBImages(newOptions);

      replace(filterNonYBImages(imageBundles));
    }
  }, [dbNodePublicInternetAccess, ybImageOptions.useYBImages, replace]);

  const osPatchingEnabled = IsOsPatchingEnabled();

  if (!osPatchingEnabled) {
    return null;
  }
  return (
    <FieldGroup
      heading={t('linuxVersionCatalog')}
      infoTitle={t('linuxVersions')}
      infoContent={t('infoContent')}
      headerAccessories={
        <RbacValidator
          accessRequiredOn={
            isEditMode ? ApiPermissionMap.MODIFY_PROVIDER : ApiPermissionMap.CREATE_PROVIDER
          }
          isControl
        >
          {imageBundles.length > 0 && (
            <YBButton
              btnIcon="fa fa-plus"
              btnText={t('addLinuxVersion')}
              btnClass="btn btn-default"
              btnType="button"
              onClick={() => toggleShowLinuxVersionModal(true)}
              disabled={isDisabled}
              data-testid="LinuxVersionCatalog-AddLinuxVersion"
            />
          )}
        </RbacValidator>
      }
    >
      <div className={classes.root}>
        <div className={classes.filters}>
          <YBTooltip
            title={
              dbNodePublicInternetAccess ? (
                ''
              ) : (
                <Typography variant="body2">
                  <Trans
                    i18nKey="linuxVersion.internetConnectivityRequired"
                    components={{
                      airGappedPrerequisitesDocLink: (
                        <Link
                          href="https://docs.yugabyte.com/preview/yugabyte-platform/prepare/server-nodes-software/#additional-software-for-airgapped-deployment"
                          target="_blank"
                          rel="noopener noreferrer"
                          color="inherit"
                          underline="always"
                        />
                      )
                    }}
                  />
                </Typography>
              )
            }
            interactive
            placement="top-start"
          >
            <span>
              <YBCheckbox
                label={t('includeYugabyteVersions')}
                onChange={(e) => {
                  const opts: YbImageOptions = {
                    ...ybImageOptions,
                    useArm: e.target.checked,
                    useX86: e.target.checked,
                    useYBImages: e.target.checked
                  };
                  setUseYBImages(opts);
                  updateImageBundles(opts, imageBundles);
                }}
                disabled={
                  isDisabled ||
                  !dbNodePublicInternetAccess ||
                  (isEditMode &&
                    (providerType === ProviderCode.AWS
                      ? isYbManagedArmImageBundleInUse || isYbManagedX86ImageBundleInUse
                      : isYbManagedX86ImageBundleInUse))
                }
                checked={ybImageOptions.useYBImages}
                inputProps={{
                  'data-testid': 'LinuxVersionCatalog-IncludeYBVersion'
                }}
              />
            </span>
          </YBTooltip>
          <YBCheckbox
            label={t('x86_64', { keyPrefix: 'universeForm.instanceConfig' })}
            onChange={(e) => {
              const opts: YbImageOptions = { ...ybImageOptions, useX86: e.target.checked };
              setUseYBImages(opts);
              updateImageBundles(opts, imageBundles);
            }}
            disabled={
              isDisabled ||
              !ybImageOptions.useYBImages ||
              (isEditMode && isYbManagedX86ImageBundleInUse)
            }
            checked={ybImageOptions.useX86}
            inputProps={{
              'data-testid': 'LinuxVersionCatalog-Includex86'
            }}
          />
          {providerType === ProviderCode.AWS && (
            <YBCheckbox
              label={t('aarch64', { keyPrefix: 'universeForm.instanceConfig' })}
              onChange={(e) => {
                const opts: YbImageOptions = { ...ybImageOptions, useArm: e.target.checked };
                setUseYBImages(opts);
                updateImageBundles(opts, imageBundles);
              }}
              disabled={
                isDisabled ||
                !ybImageOptions.useYBImages ||
                (isEditMode && isYbManagedArmImageBundleInUse)
              }
              checked={ybImageOptions.useArm}
              inputProps={{
                'data-testid': 'LinuxVersionCatalog-IncludeAarch'
              }}
            />
          )}
        </div>
        {imageBundles.length === 0 ? (
          <EmptyListPlaceholder
            variant="secondary"
            accessRequiredOn={
              isEditMode ? ApiPermissionMap.MODIFY_PROVIDER : ApiPermissionMap.CREATE_PROVIDER
            }
            actionButtonText={t('addLinuxVersion', { keyPrefix: 'linuxVersion' })}
            descriptionText={t('emptyCard.info', { keyPrefix: 'linuxVersion' })}
            onActionButtonClick={() => toggleShowLinuxVersionModal(true)}
            isDisabled={isDisabled}
            data-testid="LinuxVersionEmpty-AddLinuxVersion"
            isCustomPrimaryAction={false}
          />
        ) : (
          <LinuxVersionsList
            control={control}
            providerType={providerType}
            inUseImageBundleUuids={inUseImageBundleUuids}
            viewMode={isEditMode ? 'EDIT' : 'CREATE'}
          />
        )}
      </div>
      <AddLinuxVersionModal
        visible={showLinuxVersionModal}
        onHide={() => {
          toggleShowLinuxVersionModal(false);
        }}
        providerType={providerType}
        control={control as any}
        existingImageBundles={imageBundles}
        onSubmit={(img) => {
          append({
            ...img,
            metadata: {
              type: ImageBundleType.CUSTOM
            } as any,
            useAsDefault:
              imageBundles.filter(
                (i) => i.details.arch === img.details.arch && i.useAsDefault === true
              ).length === 0
          });
          toggleShowLinuxVersionModal(false);
        }}
      />
    </FieldGroup>
  );
};
