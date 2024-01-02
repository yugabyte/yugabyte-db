/*
 * Created on Wed Jul 19 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useRef, useState } from 'react';
import clsx from 'clsx';
import { useToggle } from 'react-use';
import { useTranslation } from 'react-i18next';
import { Box, Grid, makeStyles } from '@material-ui/core';
import Container from '../../common/Container';
import { YBButton } from '../../../../components';
import { CreateRole } from './CreateRole';
import { DeleteRoleModal } from './DeleteRoleModal';

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
    padding: theme.spacing(3),
    paddingLeft: 0,
    borderRight: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  },
  menuItem: {
    padding: '12px 16px',
    marginBottom: theme.spacing(2),
    cursor: 'pointer',
    width: '220px',
    height: '40px',
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

  const Menu = [t('menu.configurations'), t('menu.users')] as const;

  const classes = useStyles();
  const [currentView, setCurrentView] = useState<typeof Menu[number]>('Configurations');
  const createRoleRef = useRef<any>(null);

  const [showDeleteModal, toggleDeleteModal] = useToggle(false);

  return (
    <Container
      onCancel={() => {
        createRoleRef.current?.onCancel();
      }}
      onSave={() => {
        createRoleRef.current?.onSave();
      }}
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
          <YBButton
            variant="secondary"
            size="large"
            startIcon={<Delete />}
            onClick={() => {
              toggleDeleteModal(true);
            }}
          >
            {t('title', { keyPrefix: 'rbac.roles.delete' })}
          </YBButton>
        </div>
        <Grid container className={classes.addPadding}>
          <Grid item className={classes.menuContainer}>
            {Menu.map((m) => (
              <div
                key="m"
                onClick={() => {
                  setCurrentView(m);
                }}
                className={clsx(classes.menuItem, m === currentView && 'active')}
              >
                {m}
              </div>
            ))}
          </Grid>
          <Grid item>
            {currentView === t('menu.configurations') ? (
              <div>
                <CreateRole ref={createRoleRef} />
              </div>
            ) : (
              <div>Users</div>
            )}
          </Grid>
        </Grid>
        <DeleteRoleModal
          open={showDeleteModal}
          onHide={() => {
            toggleDeleteModal(false);
          }}
        />
      </Box>
    </Container>
  );
};
