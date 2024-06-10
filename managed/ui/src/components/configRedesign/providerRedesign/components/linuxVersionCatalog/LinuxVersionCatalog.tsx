/*
 * Created on Fri Nov 17 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useCallback, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useMount, useToggle } from 'react-use';
import { Control, useFieldArray, useWatch } from 'react-hook-form';
import { makeStyles } from '@material-ui/core';
import { FieldGroup } from '../../forms/components/FieldGroup';
import { YBButton, YBCheckbox } from '../../../../../redesign/components';
import { LinuxVersionEmpty } from './LinuxVersionEmpty';
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
import { ArchitectureType, ProviderCode, ProviderStatus } from '../../constants';
import { Add } from '@material-ui/icons';

interface LinuxVersionCatalogProps {
  control: Control<AWSProviderCreateFormFieldValues>;
  providerType: ProviderCode;
  viewMode: 'CREATE' | 'EDIT';
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
  viewMode,
  providerStatus
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

  const [ybImageOptions, setUseYBImages] = useState<YbImageOptions>({
    useYBImages: false,
    useArm: true,
    useX86: true
  });
  // keeps track of exisitng imageBundle values in edit mode
  const [editYBImageOptions, setEditYBImageOptions] = useState<Omit<YbImageOptions, 'useYBImages'>>(
    {
      useArm: false,
      useX86: false
    }
  );

  const [editImgBundleDetails, setEditImgBundleDetails] = useState<ImageBundle[]>([]);

  const isEditMode = viewMode === 'EDIT';

  const filterNonYBImages = (images: ImageBundle[]) => {
    return images.filter((i) => i.metadata?.type === ImageBundleType.CUSTOM);
  };

  const updateDefaultLinuxVersions = useCallback(
    (ybImageOptions: YbImageOptions, fields: ImageBundle[]) => {
      if (!isEditMode && !ybImageOptions.useYBImages) {
        replace(filterNonYBImages(fields));
        return;
      }

      const images = [...filterNonYBImages(fields)];

      if (ybImageOptions.useX86) {
        if (isEditMode && editYBImageOptions.useX86) {
          const usedImg = getYBManagedImgBundleByArch(
            editImgBundleDetails,
            ArchitectureType.X86_64
          );
          usedImg && images.push(usedImg!);
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
        if (isEditMode && editYBImageOptions.useArm) {
          const usedImg = getYBManagedImgBundleByArch(editImgBundleDetails, ArchitectureType.ARM64);
          usedImg && images.push(usedImg!);
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
    },
    [replace, editYBImageOptions, editImgBundleDetails]
  );

  useMount(() => {
    // set Options in edit mode
    const isYBAX86Used =
      getYBManagedImgBundleByArch(imageBundles, ArchitectureType.X86_64) !== undefined;
    const isYBAArmUsed =
      getYBManagedImgBundleByArch(imageBundles, ArchitectureType.ARM64) !== undefined;
    setUseYBImages({
      useArm: isYBAArmUsed,
      useX86: isYBAX86Used,
      useYBImages: providerType === ProviderCode.AWS ? isYBAArmUsed || isYBAX86Used : isYBAX86Used
    });
    setEditYBImageOptions({
      useArm: isYBAArmUsed,
      useX86: isYBAX86Used
    });
    setEditImgBundleDetails(imageBundles);
  });

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
            viewMode === 'CREATE'
              ? ApiPermissionMap.CREATE_PROVIDER
              : ApiPermissionMap.MODIFY_PROVIDER
          }
          isControl
        >
          <YBButton
            onClick={() => toggleShowLinuxVersionModal(true)}
            startIcon={<Add />}
            variant="secondary"
            data-testid="LinuxVersionCatalog-AddLinuxVersion"
          >
            {t('addLinuxVersion')}
          </YBButton>
        </RbacValidator>
      }
    >
      <div className={classes.root}>
        <div className={classes.filters}>
          <YBCheckbox
            label={t('includeYugabyteVersions')}
            onChange={(e) => {
              const opts: YbImageOptions = {
                ...ybImageOptions,
                useArm: isEditMode ? editYBImageOptions.useArm : e.target.checked,
                useX86: isEditMode ? editYBImageOptions.useX86 : e.target.checked,
                useYBImages: e.target.checked
              };
              setUseYBImages(opts);
              updateDefaultLinuxVersions(opts, imageBundles);
            }}
            disabled={
              isEditMode && providerType === ProviderCode.AWS
                ? editYBImageOptions.useArm && editYBImageOptions.useX86
                : editYBImageOptions.useX86
            }
            checked={ybImageOptions.useYBImages}
            inputProps={{
              'data-testid': 'LinuxVersionCatalog-IncludeYBVersion'
            }}
          />
          <YBCheckbox
            label={t('x86_64', { keyPrefix: 'universeForm.instanceConfig' })}
            onChange={(e) => {
              const opts: YbImageOptions = { ...ybImageOptions, useX86: e.target.checked };
              setUseYBImages(opts);
              updateDefaultLinuxVersions(opts, imageBundles);
            }}
            disabled={!ybImageOptions.useYBImages || (isEditMode && editYBImageOptions.useX86)}
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
                updateDefaultLinuxVersions(opts, imageBundles);
              }}
              disabled={!ybImageOptions.useYBImages || (isEditMode && editYBImageOptions.useArm)}
              checked={ybImageOptions.useArm}
              inputProps={{
                'data-testid': 'LinuxVersionCatalog-IncludeAarch'
              }}
            />
          )}
        </div>
        {imageBundles.length === 0 ? (
          <LinuxVersionEmpty
            viewMode={viewMode}
            onAdd={() => {
              toggleShowLinuxVersionModal(true);
            }}
          />
        ) : (
          <LinuxVersionsList control={control} providerType={providerType} viewMode={viewMode} />
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
            useAsDefault: imageBundles.filter(
              (i) => i.details.arch === img.details.arch && i.useAsDefault === true
            ).length === 0
          });
          toggleShowLinuxVersionModal(false);
        }}
      />
    </FieldGroup>
  );
};
