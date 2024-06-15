/*
 * Created on Tue Nov 21 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useEffect, useState } from 'react';
import clsx from 'clsx';
import { Control, useFieldArray, useWatch } from 'react-hook-form';
import { find, findIndex, isEmpty, split } from 'lodash';
import { useToggle } from 'react-use';
import { useTranslation } from 'react-i18next';

import { Tooltip, Typography, makeStyles } from '@material-ui/core';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import {
  ImageBundle,
  ImageBundleType
} from '../../../../../redesign/features/universe/universe-form/utils/dto';

import { MoreActionsMenu } from '../../../../customCACerts/MoreActionsMenu';
import { YBButton } from '../../../../../redesign/components';
import { AddLinuxVersionModal } from './AddLinuxVersionModal';
import { ImageBundleDefaultTag, ImageBundleYBActiveTag, IsImgBundleInUseEditEnabled } from './LinuxVersionUtils';

import { LinuxVersionDeleteModal } from './DeleteLinuxVersionModal';
import { YBPopover } from '../../../../../redesign/components/YBPopover/YBPopover';
import { ArchitectureType, ProviderCode } from '../../constants';
import { AWSProviderCreateFormFieldValues } from '../../forms/aws/AWSProviderCreateForm';
import { RbacValidator } from '../../../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../../../redesign/features/rbac/ApiAndUserPermMapping';
import { ValidationErrMsgDelimiter } from '../../forms/utils';

import styles from '../RegionList.module.scss';

import { Delete, Edit, Flag } from '@material-ui/icons';
import MoreIcon from '../../../../../redesign/assets/ellipsis.svg';
import ErrorIcon from '../../../../../redesign/assets/error.svg';

interface LinuxVersionListProps {
  control: Control<AWSProviderCreateFormFieldValues>;
  providerType: ProviderCode;
  viewMode: 'CREATE' | 'EDIT';
  inUseImageBundleUuids: Set<string>;
}

export const LinuxVersionsList: FC<LinuxVersionListProps> = ({
  control,
  providerType,
  viewMode,
  inUseImageBundleUuids
}) => {
  const { replace, update, remove } = useFieldArray({
    control,
    name: 'imageBundles'
  });

  const imageBundles: ImageBundle[] = useWatch({ name: 'imageBundles' });

  const { t } = useTranslation('translation', { keyPrefix: 'universeForm.instanceConfig' });
  const isImgBundleInUseEditEnabled = IsImgBundleInUseEditEnabled();

  const setImageAsDefault = (img: ImageBundle) => {
    const bundles = imageBundles.map((i: ImageBundle) => {
      if (i.details.arch === img.details.arch) {
        if (i.name === img.name) {
          return {
            ...i,
            useAsDefault: true
          };
        }
        return {
          ...i,
          useAsDefault: false
        };
      }
      return i;
    });

    replace(bundles);
  };

  const editImageBundle = (img: ImageBundle) => {
    const index = findIndex(
      imageBundles,
      (bundles: ImageBundle) =>
        bundles.name === img.name && bundles.details.arch === img.details.arch
    );
    update(index, {
      ...img
    });
    setEditImageBundleDetails(undefined);
  };

  const deleteImageBundle = (img: ImageBundle) => {
    const index = findIndex(
      imageBundles,
      (bundles: ImageBundle) =>
        bundles.name === img.name && bundles.details.arch === img.details.arch
    );
    remove(index);
    setDeleteImageBundleDetails(undefined);
  };

  const X86Images = imageBundles.filter(
    (i: ImageBundle) => i.details.arch === ArchitectureType.X86_64
  );
  const AarchImages = imageBundles.filter(
    (i: ImageBundle) => i.details.arch === ArchitectureType.ARM64
  );

  //if the img bundle with useAsDefault as true is deleted, select the first image as default
  useEffect(() => {
    if (X86Images.length > 0 && !find(X86Images, { useAsDefault: true })) {
      setImageAsDefault(X86Images[0]);
    }
    if (AarchImages.length > 0 && !find(AarchImages, { useAsDefault: true })) {
      setImageAsDefault(AarchImages[0]);
    }
  }, [X86Images, AarchImages]);

  const [editImageBundleDetails, setEditImageBundleDetails] = useState<ImageBundle | undefined>();
  const [deleteImageBundleDetails, setDeleteImageBundleDetails] = useState<
    ImageBundle | undefined
  >();

  const errors = control?._formState?.errors.imageBundles;

  return (
    <>
      <LinuxVersionsCard
        images={X86Images}
        archType={t('x86_64')}
        setEditDetails={(img: ImageBundle) => {
          setEditImageBundleDetails(img);
        }}
        setImageAsDefault={setImageAsDefault}
        onDelete={(img) => {
          setDeleteImageBundleDetails(img);
        }}
        viewMode={viewMode}
        inUseImageBundleUuids={inUseImageBundleUuids}
        showMoreActions={true}
        errors={errors as any}
        isImgBundleInUseEditEnabled={isImgBundleInUseEditEnabled}
      />
      <div style={{ marginTop: '24px' }} />
      {providerType === ProviderCode.AWS && (
        <LinuxVersionsCard
          images={AarchImages}
          archType={t('aarch64')}
          setEditDetails={(img: ImageBundle) => {
            setEditImageBundleDetails(img);
          }}
          setImageAsDefault={setImageAsDefault}
          onDelete={(img) => {
            setDeleteImageBundleDetails(img);
          }}
          viewMode={viewMode}
          inUseImageBundleUuids={inUseImageBundleUuids}
          showMoreActions={true}
          errors={errors as any}
          isImgBundleInUseEditEnabled={isImgBundleInUseEditEnabled}
        />
      )}

      {editImageBundleDetails && (
        <AddLinuxVersionModal
          control={control as any}
          providerType={providerType}
          onHide={() => {
            setEditImageBundleDetails(undefined);
          }}
          onSubmit={editImageBundle}
          visible={editImageBundleDetails !== undefined}
          editDetails={editImageBundleDetails}
        />
      )}
      {deleteImageBundleDetails && (
        <LinuxVersionDeleteModal
          onClose={() => {
            setDeleteImageBundleDetails(undefined);
          }}
          onSubmit={() => {
            deleteImageBundle(deleteImageBundleDetails);
          }}
          visible={true}
          imageName={deleteImageBundleDetails.name}
        />
      )}
    </>
  );
};

const LinuxVersionCardStyles = makeStyles((theme) => ({
  root: {
    borderRadius: '8px',
    padding: '20px',
    border: `1px solid ${theme.palette.ybacolors.backgroundGrayDark}`
  },
  header: {
    color: theme.palette.ybacolors.labelBackground,
    marginBottom: '8px'
  },
  moreOptionsBut: {
    borderRadius: '6px',
    border: `1px solid #C8C8C8`,
    background: theme.palette.common.white,
    height: '30px',
    width: '30px'
  },
  formatName: {
    display: 'flex',
    alignItems: 'center',
    gap: '8px'
  },
  showHideRecommendations: {
    color: theme.palette.ybacolors.textInProgress,
    textDecoration: 'underline',
    marginTop: '16px',
    cursor: 'pointer'
  },
  actionButtons: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'right',
    gap: '8px'
  }
}));

interface LinuxVersionCardCommonProps {
  setImageAsDefault: (img: ImageBundle) => void;
  images: ImageBundle[];
  archType: string;
  setEditDetails: (img: ImageBundle) => void;
  viewMode: 'CREATE' | 'EDIT';
  onDelete: (img: ImageBundle) => void;

  showTitle?: boolean;
  errors?: any[];
  isImgBundleInUseEditEnabled: boolean;
}
type LinuxVersionCardProps =
  | (LinuxVersionCardCommonProps & {
      showMoreActions: false;
    })
  | (LinuxVersionCardCommonProps & { inUseImageBundleUuids: Set<string>; showMoreActions: true });

export const LinuxVersionsCard: FC<LinuxVersionCardProps> = (props) => {
  const {
    images,
    archType,
    setEditDetails,
    setImageAsDefault,
    onDelete,
    showMoreActions,
    showTitle = true,
    viewMode,
    isImgBundleInUseEditEnabled,
    errors = []
  } = props;
  const inUseImageBundleUuids = props.showMoreActions
    ? props.inUseImageBundleUuids
    : new Set<string>();
  const classes = LinuxVersionCardStyles();
  const { t } = useTranslation('translation', { keyPrefix: 'linuxVersion.form.menuActions' });

  const [showRetiredVersions, toggleShowRetiredVersions] = useToggle(false);
  const formatActions = (image: ImageBundle, index: number) => {
    return (
      <div className={classes.actionButtons}>
        {!isEmpty(errors[index]) ? (
          <YBPopover
            hoverMsg={
              <div>
                {' '}
                {split(errors[index]?.message, ValidationErrMsgDelimiter).map((msg) => (
                  <div>{msg}</div>
                ))}
              </div>
            }
          >
            <img src={ErrorIcon} />
          </YBPopover>
        ) : null}
        <MoreActionsMenu
          menuOptions={[
            {
              text: t('edit'),
              callback: () => {
                setEditDetails(image);
              },
              icon: <Edit />,
              dataTestId: `LinuxVersionsCard${index}-Edit`,
              menuItemWrapper(elem) {
                return (
                  <RbacValidator
                    accessRequiredOn={
                      viewMode === 'CREATE'
                        ? ApiPermissionMap.CREATE_PROVIDER
                        : ApiPermissionMap.MODIFY_PROVIDER
                    }
                    isControl
                    overrideStyle={{ display: 'block' }}
                  >
                    {elem}
                  </RbacValidator>
                );
              },
              disabled: !isImgBundleInUseEditEnabled && inUseImageBundleUuids.has(image.uuid)
            },
            {
              text: t('setDefault'),
              callback: () => {
                setImageAsDefault(image);
              },
              disabled: image.useAsDefault,
              dataTestId: `LinuxVersionsCard${index}-SetDefault`,
              menuItemWrapper(elem) {
                if (!image.useAsDefault) return elem;
                return (
                  <RbacValidator
                    accessRequiredOn={
                      viewMode === 'CREATE'
                        ? ApiPermissionMap.CREATE_PROVIDER
                        : ApiPermissionMap.MODIFY_PROVIDER
                    }
                    isControl
                    overrideStyle={{ display: 'block' }}
                  >
                    <Tooltip
                      title={<Typography variant="subtitle1">{t('alreadyIsDefault')}</Typography>}
                      placement="top"
                      arrow
                    >
                      <span>{elem}</span>
                    </Tooltip>
                  </RbacValidator>
                );
              },
              icon: <Flag />
            },
            {
              text: t('delete'),
              callback: () => {
                onDelete(image);
              },
              icon: <Delete />,
              dataTestId: `LinuxVersionsCard${index}-Delete`,
              menuItemWrapper(elem) {
                return (
                  <RbacValidator
                    accessRequiredOn={
                      viewMode === 'CREATE'
                        ? ApiPermissionMap.CREATE_PROVIDER
                        : ApiPermissionMap.MODIFY_PROVIDER
                    }
                    isControl
                    overrideStyle={{ display: 'block' }}
                  >
                    {elem}
                  </RbacValidator>
                );
              },
              disabled: inUseImageBundleUuids.has(image.uuid)
            }
          ]}
        >
          <YBButton
            variant="secondary"
            className={classes.moreOptionsBut}
            data-testid={`LinuxVersionsCard${index}-MoreButton`}
          >
            <img alt="More" src={MoreIcon} width="20" />
          </YBButton>
        </MoreActionsMenu>
      </div>
    );
  };

  return (
    <div className={clsx(styles.bootstrapTableContainer, classes.root)}>
      {showTitle && (
        <Typography variant="body1" className={classes.header}>
          {archType}
        </Typography>
      )}
      <BootstrapTable
        tableContainerClass={styles.bootstrapTable}
        data={images.filter((i) =>
          showRetiredVersions ? true : i.metadata?.type !== ImageBundleType.YBA_DEPRECATED
        )}
      >
        <TableHeaderColumn dataField="uuid" isKey={true} hidden={true} />
        <TableHeaderColumn
          dataField="name"
          dataFormat={(name, row: ImageBundle) => {
            return (
              <div className={classes.formatName}>
                {name}
                {row.metadata?.type === ImageBundleType.YBA_ACTIVE && <ImageBundleYBActiveTag />}
                {row.metadata?.type === ImageBundleType.YBA_DEPRECATED && (
                  <ImageBundleDefaultTag
                    text={t('retired', { keyPrefix: 'linuxVersion.upgradeModal' })}
                    icon={<></>}
                    tooltip={t('retiredTooltip', { keyPrefix: 'linuxVersion.upgradeModal' })}
                  />
                )}
                {row.useAsDefault && <ImageBundleDefaultTag />}
              </div>
            );
          }}
        >
          <b>Name</b>
        </TableHeaderColumn>
        {showMoreActions && (
          <TableHeaderColumn
            columnClassName={styles.regionActionsColumn}
            dataFormat={(_, row, _extra, index) => formatActions(row, index)}
            dataAlign="right"
          />
        )}
      </BootstrapTable>
      {images.filter((i) => i?.metadata?.type === ImageBundleType.YBA_DEPRECATED).length > 0 && (
        <div
          onClick={() => toggleShowRetiredVersions(!showRetiredVersions)}
          className={classes.showHideRecommendations}
        >
          {showRetiredVersions
            ? t('hideRetiredVersion', { keyPrefix: 'linuxVersion' })
            : t('showRetiredVersion', { keyPrefix: 'linuxVersion' })}
        </div>
      )}
    </div>
  );
};
