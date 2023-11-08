/*
 * Created on Wed Jul 19 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useContext, useRef, useState } from 'react';
import clsx from 'clsx';
import { useToggle } from 'react-use';
import { useTranslation } from 'react-i18next';
import { Box, Grid, makeStyles } from '@material-ui/core';
import Container from '../../common/Container';
import { YBButton } from '../../../../components';
import { CreateRole } from './CreateRole';
import { UsersUnderRole } from './UsersUnderRole';
import { DeleteRoleModal } from './DeleteRoleModal';
import { ConfirmEditRoleModal } from './ConfirmEditRoleModal';

import { EditViews, RoleContextMethods, RoleViewContext } from '../RoleContext';
import { RoleType } from '../IRoles';
import { RbacValidator, hasNecessaryPerm } from '../../common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../ApiAndUserPermMapping';

import { ReactComponent as ArrowLeft } from '../../../../assets/arrow_left.svg';
import { ReactComponent as Delete } from '../../../../assets/trashbin.svg';

const useStyles = makeStyles((theme) => ({
  root: {
    padding: `0 ${theme.spacing(3)}px`
  },
  header: {
    borderBottom: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    height: '75px',
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
    fontSize: '20px',
    fontWeight: 700
  },
  menuContainer: {
    width: '242px',
    padding: '12px',
    paddingLeft: 0,
    borderRight: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  },
  menuItem: {
    padding: '12px 16px',
    marginBottom: theme.spacing(2),
    cursor: 'pointer',
    width: '220px',
    height: '42px',
    '&.active': {
      borderRadius: theme.spacing(1),
      border: '1px solid #C8C8C8',
      background: theme.palette.ybacolors.backgroundGrayLight
    }
  },
  addPadding: {
    paddingTop: theme.spacing(1.25)
  },
  back: {
    display: 'flex',
    alignItems: 'center',
    '& svg': {
      width: theme.spacing(4),
      height: theme.spacing(4),
      marginRight: theme.spacing(2),
      cursor: 'pointer'
    }
  }
}));

export const EditRole = () => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.roles.edit'
  });

  const Menu: { view: keyof typeof EditViews; label: string }[] = [
    {
      view: EditViews.CONFIGURATIONS,
      label: t('menu.configurations')
    },
    {
      view: EditViews.USERS,
      label: t('menu.users')
    }
  ];

  const classes = useStyles();
  const [
    {
      currentRole,
      formProps: { editView }
    }
  ] = (useContext(RoleViewContext) as unknown) as RoleContextMethods;
  const [currentView, setCurrentView] = useState<keyof typeof EditViews>(
    editView ?? EditViews.CONFIGURATIONS
  );
  const createRoleRef = useRef<any>(null);

  const [showDeleteModal, toggleDeleteModal] = useToggle(false);
  const [showEditRoleConfirmModal, toggleEditRoleConfirmModal] = useToggle(false);

  return (
    <Container
      onCancel={() => {
        createRoleRef.current?.onCancel();
      }}
      onSave={() => {
        toggleEditRoleConfirmModal(true);
      }}
      hideSave={currentView === EditViews.USERS}
      disableSave={
        currentRole?.roleType === RoleType.SYSTEM ||
        !hasNecessaryPerm({
          ...ApiPermissionMap.MODIFY_RBAC_ROLE,
          onResource: { ROLE: currentRole?.roleUUID }
        })
      }
    >
      <Box className={classes.root}>
        <div className={classes.header}>
          <div className={classes.back}>
            <ArrowLeft
              onClick={() => {
                createRoleRef.current?.onCancel();
              }}
            />
            {t('title')}
          </div>
          <RbacValidator
            customValidateFunction={() => {
              return (
                hasNecessaryPerm({
                  ...ApiPermissionMap.DELETE_RBAC_ROLE,
                  onResource: { ROLE: currentRole?.roleUUID }
                }) && currentRole?.roleType !== RoleType.SYSTEM
              );
            }}
            isControl
          >
            <YBButton
              variant="secondary"
              size="large"
              data-testid={`rbac-resource-delete-role`}
              startIcon={<Delete />}
              onClick={() => {
                toggleDeleteModal(true);
              }}
            >
              {t('title', { keyPrefix: 'rbac.roles.delete' })}
            </YBButton>
          </RbacValidator>
        </div>
        <Grid container className={classes.addPadding}>
          <Grid item className={classes.menuContainer}>
            {Menu.map((m) => (
              <div
                key={m.label}
                onClick={() => {
                  setCurrentView(m.view);
                }}
                className={clsx(classes.menuItem, m.view === currentView && 'active')}
              >
                {m.label}
              </div>
            ))}
          </Grid>
          <Grid item>
            {currentView === EditViews.CONFIGURATIONS ? (
              <div>
                <CreateRole ref={createRoleRef} />
              </div>
            ) : (
              <UsersUnderRole ref={createRoleRef} />
            )}
          </Grid>
        </Grid>
        <DeleteRoleModal
          open={showDeleteModal}
          onHide={() => {
            toggleDeleteModal(false);
          }}
        />
        <ConfirmEditRoleModal
          open={showEditRoleConfirmModal}
          onHide={() => {
            toggleEditRoleConfirmModal(false);
          }}
          onSubmit={() => {
            toggleEditRoleConfirmModal(false);
            createRoleRef.current?.onSave();
          }}
        />
      </Box>
    </Container>
  );
};
