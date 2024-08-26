/*
 * Created on Tue Aug 06 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useState } from 'react';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { Control, useController } from 'react-hook-form';

import {
  Accordion,
  AccordionDetails,
  AccordionSummary,
  makeStyles,
  Typography
} from '@material-ui/core';
import { YBCheckboxField } from '../../../../../../components';

import { BackupObjectsModel } from '../../models/IBackupObjects';

import { ArrowDropDown } from '@material-ui/icons';
import Checked from '../../../../../../assets/checkbox/Checked.svg';
import UnChecked from '../../../../../../assets/checkbox/UnChecked.svg';

const useStyles = makeStyles((theme) => ({
  root: {
    border: 'none'
  },
  tableSpace: {
    width: '550px',
    height: '90px',
    padding: '8px 8px',
    borderRadius: '8px',
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    cursor: 'pointer',
    '&:hover': {
      backgroundColor: theme.palette.ybacolors.backgroundBlueLight
    }
  },
  helpText: {
    color: theme.palette.ybacolors.textDarkGray,
    marginLeft: '36px'
  },
  header: {
    padding: 0,
    '& svg': {
      fontSize: '24px'
    },
    '& .MuiAccordionSummary-content': {
      alignItems: 'center'
    }
  },
  expanded: {
    transform: 'rotate(180deg)'
  }
}));

interface BackupTablespaceProps {
  control: Control<BackupObjectsModel, any>;
}

export const BackupTablespace: FC<BackupTablespaceProps> = ({ control }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.scheduled.create.backupObjects'
  });

  const [isExpanded, setIsExpanded] = useState(false);

  const classes = useStyles();

  const { field } = useController({
    control,
    name: 'useTablespaces',
    defaultValue: false
  });

  const toggleValue = (e: React.MouseEvent<HTMLElement>) => {
    field.onChange(!field.value);
    e.preventDefault();
    e.stopPropagation();
  };

  return (
    <div>
      <Accordion
        className={classes.root}
        onClick={() => setIsExpanded(!isExpanded)}
        expanded={isExpanded}
      >
        <AccordionSummary className={classes.header}>
          <ArrowDropDown className={clsx(isExpanded && classes.expanded)} />
          <Typography>{t('advancedConfig')}</Typography>
        </AccordionSummary>
        <AccordionDetails>
          <div className={classes.tableSpace} onClick={(e) => toggleValue(e)}>
            <YBCheckboxField
              label={<span onClick={(e) => toggleValue(e)}>{t('backupTablespace')}</span>}
              control={control}
              name="useTablespaces"
              icon={<img src={UnChecked} alt="unchecked" />}
              checkedIcon={<img src={Checked} alt="checked" />}
              data-testid="useTablespaces"
            />
            <Typography className={classes.helpText} variant="body2">
              {t('backupTablespaceSubText')}
            </Typography>
          </div>
        </AccordionDetails>
      </Accordion>
    </div>
  );
};
