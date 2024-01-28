/*
 * Created on Mon Jun 26 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { useEffect, useState } from 'react';
import clsx from 'clsx';
import { FieldValues } from 'react-hook-form';
import { useMap } from 'react-use';
import { useTranslation } from 'react-i18next';
import { find, values } from 'lodash';
import Select, { components } from 'react-select';
import {
  Box,
  Checkbox,
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableRow,
  Typography,
  makeStyles
} from '@material-ui/core';
import { YBCheckbox } from '../../../../../../redesign/components';
import { YBSearchInput } from '../../../../../common/forms/fields/YBSearchInput';
import Checked from '../../../../../../redesign/assets/checkbox/Checked.svg';
import UnChecked from '../../../../../../redesign/assets/checkbox/UnChecked.svg';
import Intermediate from '../../../../../../redesign/assets/checkbox/Intermediate.svg';

type YBTableProps<T> = {
  table: T[];
  tableHeader: string[];
  name: keyof FieldValues;
  sortFn?: (tables: T[]) => T[];
  setValue: (table: T[]) => void;
  defaultValues: T[];
  searchPlaceholder: string;
  renderBodyFn?: (row: T) => React.ReactChild;
  searchFn?: (row: T, searchText: string) => boolean;
  tableCountInfo?: (selectedRows: T[]) => React.ReactChild;
  customComponents?: () => React.ReactChild;
  overrideStyles?: {
    actionsClassname?: string
  };
  disableSelection?: boolean;
};

type FilterOptions = 'ALL' | 'SELECTED';

const useStyles = makeStyles((theme) => ({
  actions: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center',
    marginBottom: theme.spacing(2)
  },
  ybSearchInput: {
    '& .search-input': {
      width: '630px'
    }
  },
  viewSelection: {
    width: '150px',
    height: '42px',
    color: '#C8C8C8',
    boxShadow: 'none'
  },
  tablesCount: {
    color: '#67666C',
    textAlign: 'right',
    width: '100%',
    fontWeight: 400
  },
  table: {
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: theme.spacing(1),
    minHeight: '550px',
    '& .MuiTableCell-body, .MuiTableCell-head': {
      padding: `${theme.spacing(1.5)}px ${theme.spacing(1.25)}px`,
      paddingLeft: theme.spacing(1.5)
    },
    clear: 'both'
  },
  tableHead: {
    height: '40px',
    color: theme.palette.grey[900],
    fontSize: '13px',
    fontWeight: 400,
    textTransform: 'uppercase',
    padding: 0
  },
  tableBody: {
    '& .MuiFormControlLabel-root': {
      marginRight: 0
    }
  },
  tableCell: {
    paddingLeft: '0 !important',
    height: '40px'
  },
  checkboxDisabled: {
    opacity: '0.5'
  }
}));

const getDefaultValues = <T,>(allValues: T[], selected: T[]) => {
  return allValues.reduce((prev, cur, ind) => {
    if (cur && find(selected, cur)) {
      prev[ind] = cur;
    }
    return prev;
  }, {});
};

export const YBTable = <T,>(props: YBTableProps<T>) => {
  const { tableHeader, table, name, setValue, defaultValues, searchPlaceholder, renderBodyFn, searchFn, tableCountInfo, customComponents, overrideStyles, disableSelection = false } = props;

  const [selected, { set, setAll, get, remove }] = useMap<typeof table>(
    (getDefaultValues(table, defaultValues) as unknown) as T[]
  );

  const selectedEntriesCount = Object.keys(selected).length;

  const [searchText, setSearchText] = useState('');

  const classes = useStyles();
  const { t } = useTranslation();

  const tableFilterOptions: { value: FilterOptions; label: string }[] = [
    {
      label: t('newRestoreModal.selectTables.viewFilter.viewAll'),
      value: 'ALL'
    },
    {
      label: t('newRestoreModal.selectTables.viewFilter.viewSelected'),
      value: 'SELECTED'
    }
  ];

  const [viewFilter, setViewFilter] = useState(tableFilterOptions[0]);

  useEffect(() => {
    setValue(values(selected));
  }, [selected, setValue]);

  const getTableHeader = () => {
    return (
      <TableHead className={classes.tableHead}>
        <TableRow>
          <TableCell width={8}>
            <Checkbox
              indeterminate={selectedEntriesCount > 0 && selectedEntriesCount < table.length}
              checked={selectedEntriesCount === table.length}
              onChange={(_, state) => {
                if (state) {
                  setAll(Object.fromEntries(table.map((obj, i) => [i, obj])) as any);
                } else {
                  setAll([]);
                }
              }}
              icon={<img src={UnChecked} alt="unchecked" />}
              checkedIcon={<img src={Checked} alt="checked" />}
              indeterminateIcon={<img src={Intermediate} alt="intermediate" />}
              disabled={disableSelection}
              className={clsx(disableSelection && classes.checkboxDisabled)}
            />
          </TableCell>
          {tableHeader.map((title, i) => (
            <TableCell className={classes.tableCell} key={i}>
              {title}
            </TableCell>
          ))}
        </TableRow>
      </TableHead>
    );
  };

  const getTableBody = () => {
    return (
      <TableBody className={classes.tableBody}>
        {table.map((row, i) => {
          if (viewFilter.value === 'SELECTED') {
            if (!get(i)) {
              return null;
            }
          }

          if (searchText) {
            if (typeof row === 'string') {
              if (!row.includes(searchText)) {
                return null;
              }
            }
            if (searchFn?.(row, searchText) === false) {
              return null;
            }
          }

          return (
            <TableRow key={i}>
              <TableCell width={8}>
                <YBCheckbox
                  name={`${name}.${i}`}
                  label=""
                  checked={!!get(i)}
                  onChange={(_, state) => {
                    if (state) {
                      set(i, row);
                    } else {
                      remove(i);
                    }
                  }}
                  icon={<img src={UnChecked} alt="unchecked" />}
                  checkedIcon={<img src={Checked} alt="checked" />}
                  disabled={disableSelection}
                />
              </TableCell>
              <TableCell className={classes.tableCell}>{renderBodyFn?.(row) ?? row}</TableCell>
            </TableRow>
          );
        })}
      </TableBody>
    );
  };

  return (
    <>
      <Box className={clsx(classes.actions, overrideStyles?.actionsClassname)}>
        <div className={classes.ybSearchInput}>
          <YBSearchInput
            val={searchText}
            onValueChanged={(e: React.ChangeEvent<HTMLInputElement>) => {
              setSearchText(e.target.value);
            }}
            placeHolder={searchPlaceholder}
          />
        </div>
        <div>
          <Select
            onChange={(val) => setViewFilter(val as any)}
            value={viewFilter}
            options={tableFilterOptions}
            components={{
              // eslint-disable-next-line react/display-name
              Control: ({ children, className, ...rest }) => (
                <components.Control {...rest} className={clsx(classes.viewSelection, className)}>
                  {children}
                </components.Control>
              )
            }}

            styles={{
              control: styles => ({ ...styles, borderRadius: '8px', height: '42px' })
            }}
          />
        </div>
        <div className={classes.tablesCount}>
          <Typography variant="body2">
            {tableCountInfo?.(values(selected))}
          </Typography>
        </div>
      </Box>
      {customComponents?.()}
      <Box className={classes.table}>
        <Table>
          {getTableHeader()}
          {getTableBody()}
        </Table>
      </Box>
    </>
  );
};
