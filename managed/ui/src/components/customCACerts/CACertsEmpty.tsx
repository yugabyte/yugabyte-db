/*
 * Created on Mon Jun 05 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { Grid, makeStyles } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { YBButton } from '../../redesign/components';
import { RbacValidator } from '../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../redesign/features/rbac/ApiAndUserPermMapping';

type CACertsEmptyProps = {
  onUpload: () => void;
};

const UPLOAD_ICON = <i className="fa fa-upload ca-cert-upload-icon" />;

const useStyles = makeStyles((theme) => ({
  root: {
    background: `#fafafb`,
    border: `1px dashed #C8C7CE`,
    borderRadius: theme.spacing(1),
    display: 'flex',
    justifyContent: 'center',
    alignItems: 'center',
    textAlign: 'center',
    flexDirection: 'column',
    height: '300px',
    gap: theme.spacing(2),
    whiteSpace: 'pre-wrap',
    '& .ca-cert-upload-icon': {
      color: theme.palette.ybacolors.ybBorderGray,
      fontSize: '38px',
      marginBottom: theme.spacing(2)
    },
    '& button': {
      height: '40px'
    }
  }
}));

export const CACertsEmpty: FC<CACertsEmptyProps> = ({ onUpload }) => {

  const { t } = useTranslation();
  const classes = useStyles();

  return (
    <Grid className={classes.root} container>
      <Grid item>{UPLOAD_ICON}</Grid>
      <Grid item>
        <RbacValidator
          accessRequiredOn={ApiPermissionMap.CREATE_CA_CERT}
          isControl
        >
          <YBButton variant="primary" onClick={() => onUpload()} data-testid="uploadCACertBut">
            {t('customCACerts.uploadCACertModal.title')}
          </YBButton>
        </RbacValidator>
      </Grid>
      <Grid item>{t('customCACerts.listing.caCertsListEmpty')}</Grid>
    </Grid>
  );
};
