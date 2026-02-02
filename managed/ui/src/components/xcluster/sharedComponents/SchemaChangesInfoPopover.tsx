import React, { useState, useEffect, ReactElement } from 'react';
import { Box, makeStyles, Popover, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import clsx from 'clsx';

import {
  I18N_KEY_PREFIX_XCLUSTER_TERMS,
  XClusterSchemaChangeMode,
  XCLUSTER_DR_DDL_STEPS_DOCUMENTATION_URL,
  XCLUSTER_REPLICATION_DDL_STEPS_DOCUMENTATION_URL
} from '../constants';
import { usePillStyles } from '@app/redesign/styles/styles';

import UnavailableIcon from '../../../redesign/assets/unavailable.svg';

interface InfoPopoverProps {
  isDrInterface: boolean;
  schemaChangeMode: XClusterSchemaChangeMode;
  children: ReactElement;
}

const useStyles = makeStyles((theme) => ({
  popoverPaper: {
    overflow: 'visible !important',
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    boxShadow: '0px 3px 15px 0px rgba(102, 102, 102, 0.15)'
  },
  popoverCanvas: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1.5),

    padding: theme.spacing(3, 2),
    width: '280px',
    position: 'relative',

    '&::before, &::after': {
      content: '""',
      position: 'absolute',
      top: '20%',
      right: '100%',
      border: 'solid transparent',
      height: 0,
      width: 0,
      pointerEvents: 'none'
    },
    '&::before': {
      borderRightColor: theme.palette.ybacolors.ybBorderGray,
      borderWidth: 11,
      marginTop: -11
    },
    '&::after': {
      borderRightColor: theme.palette.background.paper,
      borderWidth: 10,
      marginTop: -10
    }
  },
  importantTag: {
    height: 24
  },
  closeButton: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(0.5),

    color: theme.palette.grey[600],

    '&:hover': {
      cursor: 'pointer'
    }
  },
  buttonStyledLink: {
    padding: theme.spacing(1, 2),

    lineHeight: '16px',
    borderRadius: theme.shape.borderRadius,
    backgroundColor: theme.palette.orange[500],
    color: theme.palette.common.white,
    textDecoration: 'none',

    '&:hover': {
      color: theme.palette.common.white
    },
    '&:focus': {
      color: theme.palette.common.white
    }
  }
}));

const ACKNOWLEDGED_STATUS_LOCAL_STORAGE_KEY = 'xClusterSchemaChangesInfoAcknowledged';
const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.shared.schemaChangesInfoPopover';

export const SchemaChangesInfoPopover: React.FC<InfoPopoverProps> = ({
  isDrInterface,
  schemaChangeMode,
  children
}) => {
  const [anchorEl, setAnchorEl] = useState<HTMLElement | null>(null);
  const [isAcknowledged, setIsAcknowledged] = useState<boolean>(true);
  const classes = useStyles();
  const pillClasses = usePillStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const childElementId = children.props.id ?? ACKNOWLEDGED_STATUS_LOCAL_STORAGE_KEY;
  useEffect(() => {
    const acknowledged = localStorage.getItem(ACKNOWLEDGED_STATUS_LOCAL_STORAGE_KEY);
    setIsAcknowledged(acknowledged === 'true');

    if (acknowledged !== 'true') {
      // Open the popover by default if not acknowledged
      const childElement = document.getElementById(childElementId);
      if (childElement) {
        setAnchorEl(childElement);
      }
    }
  }, []);

  const handleClick = (event: React.MouseEvent<HTMLElement>) => {
    setAnchorEl(event.currentTarget);
  };

  const handleClose = () => {
    setAnchorEl(null);
    if (!isAcknowledged) {
      localStorage.setItem(ACKNOWLEDGED_STATUS_LOCAL_STORAGE_KEY, 'true');
      setIsAcknowledged(true);
    }
  };

  const open = Boolean(anchorEl);
  const xClusterOffering = t(`offering.${isDrInterface ? 'dr' : 'xClusterReplication'}`, {
    keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
  });
  const learnMoreUrl = isDrInterface
    ? XCLUSTER_DR_DDL_STEPS_DOCUMENTATION_URL
    : XCLUSTER_REPLICATION_DDL_STEPS_DOCUMENTATION_URL;
  const schemaChangeInstructionsI18nKey = `${TRANSLATION_KEY_PREFIX}.description.${
    schemaChangeMode === XClusterSchemaChangeMode.AUTOMATIC_DDL_REPLICATION
      ? 'automaticSchemaChangeMode'
      : schemaChangeMode === XClusterSchemaChangeMode.DB_SCOPED
      ? 'dbScopedSchemaChangeMode'
      : 'tableLevelSchemaChangeMode'
  }`;
  return (
    <>
      {React.cloneElement(children, {
        onClick: (e: React.MouseEvent<HTMLElement>) => {
          e.stopPropagation();
          handleClick(e);
        },
        id: childElementId
      })}
      <Popover
        open={open}
        anchorEl={anchorEl}
        onClose={handleClose}
        anchorOrigin={{
          vertical: 'center',
          horizontal: 'right'
        }}
        transformOrigin={{
          vertical: 50,
          horizontal: -15
        }}
        PaperProps={{
          className: classes.popoverPaper
        }}
      >
        <div className={classes.popoverCanvas}>
          <div className={clsx(pillClasses.pill, pillClasses.metadataGrey, classes.importantTag)}>
            {t('important')}
          </div>
          <Typography variant="h6">{t('title')}</Typography>
          <Typography variant="body2">
            <Trans
              i18nKey={schemaChangeInstructionsI18nKey}
              components={{ bold: <b /> }}
              values={{
                xClusterOffering: xClusterOffering,
                sourceUniverseTerm: t(`source.${isDrInterface ? 'dr' : 'xClusterReplication'}`, {
                  keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
                }),
                targetUniverseTerm: t(`target.${isDrInterface ? 'dr' : 'xClusterReplication'}`, {
                  keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
                })
              }}
            />
          </Typography>
          <Box display="flex" justifyContent="space-between" marginTop={5}>
            <div className={classes.closeButton} onClick={handleClose}>
              <UnavailableIcon />
              {t('close')}
            </div>
            <a
              href={learnMoreUrl}
              target="_blank"
              rel="noopener noreferrer"
              className={classes.buttonStyledLink}
            >
              {t('learnMore')}
            </a>
          </Box>
        </div>
      </Popover>
    </>
  );
};
