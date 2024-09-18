/*
 * Created on Tue Jul 23 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { makeStyles, Typography } from '@material-ui/core';
import { AlertVariant, YBAlert, YBButton } from '../../../../components';
import { Trans, useTranslation } from 'react-i18next';
import { Add } from '@material-ui/icons';
import { Pages } from './GroupContext';
import { GetGroupContext } from './GroupUtils';
import { ReactComponent as UserGroupsIcon } from '../../../../assets/user-group.svg';
import { ReactComponent as AnnouncementIcon } from '../../../../assets/announcement.svg';

const useStyles = makeStyles((theme) => ({
  wrapper: {
    padding: '20px 24px'
  },
  root: {
    display: 'flex',
    justifyContent: 'center',
    alignItems: 'center',
    flexDirection: 'column',
    gap: '24px',
    height: '320px',
    borderRadius: '8px',
    background: '#FAFAFB',
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  },
  helpText: {
    fontSize: '14px',
    fontWeight: 400
  },
  link: {
    color: 'inherit',
    textDecoration: 'underline'
  }
}));

interface GroupEmptyProps {
  noAuthProviderConfigured?: boolean;
}

export const GroupEmpty: FC<GroupEmptyProps> = ({ noAuthProviderConfigured = false }) => {
  const classes = useStyles();

  const [, { setCurrentPage, setCurrentGroup }] = GetGroupContext();

  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.groups.empty'
  });

  return (
    <div className={classes.wrapper}>
      <div className={classes.root}>
        <UserGroupsIcon />
        <YBButton
          variant="primary"
          size="large"
          startIcon={<Add />}
          onClick={() => {
            setCurrentGroup(null);
            setCurrentPage(Pages.CREATE_GROUP);
          }}
          disabled={noAuthProviderConfigured}
          data-testid="create-group-button"
        >
          {t('addGroup')}
        </YBButton>
        <Typography variant="body2" className={classes.helpText}>
          {t('addGroupHelpText')}
        </Typography>
        {noAuthProviderConfigured && (
          <YBAlert
            variant={AlertVariant.Warning}
            icon={<AnnouncementIcon />}
            open
            text={
              <Trans
                t={t}
                i18nKey="noAuthConfigured"
                components={{
                  a: (
                    <a
                      className={classes.link}
                      href="/admin/rbac?tab=user-auth"
                      rel="noreferrer"
                      target="_blank"
                    ></a>
                  ),
                  b: <b />
                }}
              />
            }
          />
        )}
      </div>
    </div>
  );
};
