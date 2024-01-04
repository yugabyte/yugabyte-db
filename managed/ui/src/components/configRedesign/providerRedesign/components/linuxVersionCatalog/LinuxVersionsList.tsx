/*
 * Created on Tue Nov 21 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useState } from 'react';
import clsx from 'clsx';
import { Control, useFieldArray, useWatch } from 'react-hook-form';
import { findIndex } from 'lodash';
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
import { ImageBundleDefaultTag, ImageBundleYBActiveTag } from './LinuxVersionUtils';

import { LinuxVersionDeleteModal } from './DeleteLinuxVersionModal';
import { ArchitectureType, ProviderCode } from '../../constants';
import { AWSProviderCreateFormFieldValues } from '../../forms/aws/AWSProviderCreateForm';

import styles from '../RegionList.module.scss';

import { Delete, Edit, Flag } from '@material-ui/icons';
import MoreIcon from '../../../../../redesign/assets/ellipsis.svg';

interface LinuxVersionEmptyProps {
  control: Control<AWSProviderCreateFormFieldValues>;
  providerType: ProviderCode;
}

export const LinuxVersionsList: FC<LinuxVersionEmptyProps> = ({ control, providerType }) => {
  const { replace, update, remove } = useFieldArray({
    control,
    name: 'imageBundles'
  });

  const imageBundles = useWatch({ name: 'imageBundles' });

  const { t } = useTranslation('translation', { keyPrefix: 'universeForm.instanceConfig' });

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

  const [editImageBundleDetails, setEditImageBundleDetails] = useState<ImageBundle | undefined>();
  const [deleteImageBundleDetails, setDeleteImageBundleDetails] = useState<
    ImageBundle | undefined
  >();

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
  }
}));

interface LinuxVersionCardProps {
  setImageAsDefault: (img: ImageBundle) => void;
  images: ImageBundle[];
  archType: string;
  setEditDetails: (img: ImageBundle) => void;
  onDelete: (img: ImageBundle) => void;
  showMoreActions?: boolean;
  showTitle?: boolean;
}

export const LinuxVersionsCard: FC<LinuxVersionCardProps> = ({
  images,
  archType,
  setEditDetails,
  setImageAsDefault,
  onDelete,
  showMoreActions = true,
  showTitle = true
}) => {
  const classes = LinuxVersionCardStyles();
  const { t } = useTranslation('translation', { keyPrefix: 'linuxVersion.form.menuActions' });

  const [showRetiredVersions, toggleShowRetiredVersions] = useToggle(false);

  const formatActions = (image: ImageBundle) => {
    return (
      <MoreActionsMenu
        menuOptions={[
          {
            text: t('edit'),
            callback: () => {
              setEditDetails(image);
            },
            icon: <Edit />
          },
          {
            text: t('setDefault'),
            callback: () => {
              setImageAsDefault(image);
            },
            disabled: image.useAsDefault,
            menuItemWrapper(elem) {
              if (!image.useAsDefault) return elem;
              return (
                <Tooltip
                  title={<Typography variant="subtitle1">{t('alreadyIsDefault')}</Typography>}
                  placement="top"
                  arrow
                >
                  <span>{elem}</span>
                </Tooltip>
              );
            },
            icon: <Flag />
          },
          {
            text: t('delete'),
            callback: () => {
              onDelete(image);
            },
            icon: <Delete />
          }
        ]}
      >
        <YBButton variant="secondary" className={classes.moreOptionsBut}>
          <img alt="More" src={MoreIcon} width="20" />
        </YBButton>
      </MoreActionsMenu>
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
            dataFormat={(_, row) => formatActions(row)}
            dataAlign="right"
          />
        )}
      </BootstrapTable>
      {images.filter((i) => i.metadata.type === ImageBundleType.YBA_DEPRECATED).length > 0 && (
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
