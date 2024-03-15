/*
 * Created on Mon Dec 04 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import clsx from 'clsx';
import { Trans, useTranslation } from 'react-i18next';
import { YBModal } from '../../../../../redesign/components';
import { makeStyles } from '@material-ui/core';

interface LinuxVersionDeleteModalProps {
  visible: boolean;
  onSubmit: () => void;
  onClose: () => void;
  imageName: string;
}

const useStyles = makeStyles((theme) => ({
  titleIcon: {
    width: '20px',
    height: '20px',
    color: theme.palette.ybacolors.ybIcon
  },
  root: {
    padding: '28px 20px'
  }
}));

export const LinuxVersionDeleteModal: FC<LinuxVersionDeleteModalProps> = ({
  onClose,
  onSubmit,
  visible,
  imageName
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'linuxVersion.deleteModal' });

  const classes = useStyles();

  return (
    <YBModal
      open={visible}
      onClose={onClose}
      onSubmit={onSubmit}
      title={t('title')}
      titleIcon={<i className={clsx('fa fa-trash', classes.titleIcon)} />}
      size="sm"
      overrideHeight={'300px'}
      overrideWidth={'535px'}
      dialogContentProps={{
        dividers: true,
        className: classes.root
      }}
      submitLabel={t('delete', { keyPrefix: 'common' })}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
    >
      <Trans
        i18nKey={`linuxVersion.deleteModal.infoContent`}
        components={{
          b: <b />,
          br: <br />
        }}
        values={{
          image_name: imageName
        }}
      />
    </YBModal>
  );
};
