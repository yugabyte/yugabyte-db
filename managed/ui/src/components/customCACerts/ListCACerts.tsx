/*
 * Created on Mon Jun 05 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC, useState } from 'react';
import { Box, Grid, Typography, makeStyles } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { TableHeaderColumn } from 'react-bootstrap-table';
import DeleteCACertModal from './DeleteCACertModal';
import UploadCACertModal from './UploadCACertModal';
import { CACert } from './ICerts';
import { MoreActionsMenu } from './MoreActionsMenu';
import { downloadCert } from './CertUtils';
import { getCertDetails } from './CACertsApi';
import { CACertsEmpty } from './CACertsEmpty';
import { getCACertsList } from './CACertsApi';
import { YBButton } from '../../redesign/components';
import { YBLoadingCircleIcon } from '../common/indicators';
import { YBTable } from '../common/YBTable';
import { ybFormatDate } from '../../redesign/helpers/DateUtils';
import { MoreHoriz } from '@material-ui/icons';
import { RbacValidator, hasNecessaryPerm } from '../../redesign/features/rbac/common/RbacValidator';
import { UserPermissionMap } from '../../redesign/features/rbac/UserPermPathMapping';

type ListCACertsProps = {};

const useStyles = makeStyles((theme) => ({
  root: {
    background: theme.palette.common.white,
    border: `1px solid #E5E5E9`,
    borderRadius: theme.spacing(1),
    padding: theme.spacing(3),
    minHeight: '745px',
    '& .yb-table': {
      marginTop: theme.spacing(3)
    }
  },
  createCertButton: {
    float: 'right',
    height: theme.spacing(5)
  },
  deleteMenu: {
    color: '#E73E36'
  },
  moreIcon: {
    fontSize: theme.spacing(3)
  }
}));

const ListCACerts: FC<ListCACertsProps> = () => {
  const { t } = useTranslation();
  const classes = useStyles();
  const [showUploadCACertModal, setShowUploadCACertModal] = useState(false);
  const [showDeleteCACertModal, setShowDeleteCACertModal] = useState(false);

  const { data: caCerts, isLoading } = useQuery('ca_certs_list', getCACertsList);

  const [selectedCADetails, setSelectedCADetails] = useState<CACert | null>(null);

  if (isLoading) return <YBLoadingCircleIcon />;

  const getActions = (cert: CACert) => {
    return (
      <MoreActionsMenu
        menuOptions={[
          {
            text: t('customCACerts.listing.table.moreActions.download'),
            callback: () => {
              getCertDetails(cert.id).then((resp) => {
                downloadCert(resp.data);
              });
            }
          },
          {
            text: t('customCACerts.listing.table.moreActions.updateCert'),
            callback: () => {
              if (!hasNecessaryPerm({ ...UserPermissionMap.createCACerts })) {
                return;
              }
              setSelectedCADetails(cert);
              setShowUploadCACertModal(true);
            },
            menuItemWrapper(elem) {
              return <RbacValidator accessRequiredOn={{ ...UserPermissionMap.createCACerts }} isControl>{elem}</RbacValidator>;
            },
          },
          {
            text: '',
            isDivider: true,
            callback: () => { }
          },
          {
            text: t('common.delete'),
            className: classes.deleteMenu,
            callback: () => {
              if (!hasNecessaryPerm({ ...UserPermissionMap.deleteCACerts })) {
                return;
              }
              setSelectedCADetails(cert);
              setShowDeleteCACertModal(true);
            },
            menuItemWrapper(elem) {
              return <RbacValidator accessRequiredOn={{ ...UserPermissionMap.deleteCACerts }} isControl>{elem}</RbacValidator>;
            },
          }
        ]}
      >
        <MoreHoriz className={classes.moreIcon} />
      </MoreActionsMenu>
    );
  };

  return (
    <Box className={classes.root}>
      <RbacValidator
        accessRequiredOn={{
          ...UserPermissionMap.listCACerts
        }}
      >
        {caCerts?.data.length === 0 ? (
          <CACertsEmpty onUpload={() => setShowUploadCACertModal(true)} />
        ) : (
          <>
            <Grid container>
              <Grid item xs={12}>
                <Typography variant="h3">{t('customCACerts.listing.header')}</Typography>
              </Grid>
              <Grid item xs={12} alignItems="flex-end">
                <RbacValidator
                  accessRequiredOn={{
                    ...UserPermissionMap.createCACerts
                  }}
                  isControl
                  overrideStyle={{
                    float: 'right'
                  }}
                >
                  <YBButton
                    className={classes.createCertButton}
                    variant="primary"
                    onClick={() => setShowUploadCACertModal(true)}
                    data-testid="uploadCACertBut"
                  >
                    {t('customCACerts.listing.addCertButton')}
                  </YBButton>
                </RbacValidator>
              </Grid>
              <YBTable data={caCerts?.data ?? []}>
                <TableHeaderColumn dataField="id" isKey={true} hidden={true} />
                <TableHeaderColumn dataField="name" width="25%" dataSort>
                  {t('customCACerts.listing.table.name')}
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="createdTime"
                  dataFormat={(time) => ybFormatDate(time)}
                  width="25%"
                  dataSort
                >
                  {t('customCACerts.listing.table.created')}
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="startDate"
                  dataFormat={(time) => ybFormatDate(time)}
                  width="25%"
                  dataSort
                >
                  {t('customCACerts.listing.table.validFrom')}
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="expiryDate"
                  dataFormat={(time) => ybFormatDate(time)}
                  width="25%"
                  dataSort
                >
                  {t('customCACerts.listing.table.validUntil')}
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="actions"
                  dataAlign="left"
                  dataFormat={(_, row) => getActions(row)}
                  width="2%"
                />
              </YBTable>
            </Grid>
          </>
        )}

        <UploadCACertModal
          visible={showUploadCACertModal}
          onHide={() => {
            setSelectedCADetails(null);
            setShowUploadCACertModal(false);
          }}
          editCADetails={selectedCADetails}
        />
        <DeleteCACertModal
          deleteCADetails={selectedCADetails}
          visible={showDeleteCACertModal}
          onHide={() => {
            setSelectedCADetails(null);
            setShowDeleteCACertModal(false);
          }}
        />
      </RbacValidator>
    </Box>
  );
};

export default ListCACerts;
