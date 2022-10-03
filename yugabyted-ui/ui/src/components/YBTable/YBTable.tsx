import React, { ReactElement } from 'react';
import MUIDataTable, {
  MUIDataTableColumn,
  MUIDataTableCustomHeadRenderer,
  MUIDataTableOptions,
  MUISortOptions,
  MUIDataTableCheckboxProps
} from 'mui-datatables';
import { createStyles, makeStyles, Paper, TableCell, TableContainer, TableSortLabel, Theme } from '@material-ui/core';
import { ArrowDropUp, ArrowDropDown } from '@material-ui/icons';
import { YBCheckbox } from '@app/components/YBCheckbox/YBCheckbox';
import { YBRadio } from '@app/components/YBRadio/YBRadio';
import clsx from 'clsx';

interface YBTableProps {
  title?: string;
  selectableRows?: 'single' | 'multiple' | 'none';
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  data: (Record<any, any> | number[] | string[])[];
  columns: MUIDataTableColumn[];
  options?: MUIDataTableOptions;
  withBorder?: boolean;
}

interface ColumnMeta extends MUIDataTableCustomHeadRenderer {
  hideHeader?: boolean;
}

const useStyles = makeStyles((theme: Theme) => {
  return createStyles({
    root: {
      '& .MuiTableSortLabel-root': {
        maxHeight: theme.spacing(3)
      },
      '& .expandRow': {
        '& .MuiTableCell-root': {
          borderBottom: 0
        },
        '&:hover': {
          backgroundColor: theme.palette.common.white
        },
        '+ tr': {
          '&:hover': {
            backgroundColor: theme.palette.common.white
          }
        }
      }
    },
    noBorderContainer: {
      '& .MuiPaper-root': {
        border: 0
      },
      '& .MuiTableContainer-root': {
        padding: 0,
        margin: 0
      }
    },
    dropdown: {
      display: 'inline-grid'
    },
    expandClass: {
      width: theme.spacing(3)
    }
  });
});

// DropdownIndicator component is for up-down arrows for sorting column
const DropdownIndicator = (props: { active: boolean; direction: string }) => {
  const classes = useStyles();

  return (
    <div className={classes.dropdown}>
      <ArrowDropUp viewBox="1 -9 24 24" color={props.active && props.direction === 'asc' ? 'primary' : 'disabled'} />
      <ArrowDropDown viewBox="1 9 24 24" color={props.active && props.direction === 'desc' ? 'primary' : 'disabled'} />
    </div>
  );
};

// cHeadRender component is for up-down arrows for sorting column
// If don't want to show header then add label = 'Empty' in columns list
const cHeadRender = (
  header: ColumnMeta,
  updateDirection: (arg0: number) => void,
  sortOrder: MUISortOptions,
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  headerProps?: (meta: ColumnMeta) => Record<any, any>,
  sort?: boolean
) => {
  let restProps = {};
  if (headerProps) {
    restProps = { ...headerProps(header) };
  }
  return (
    <TableCell key={header.name} onClick={() => updateDirection(header.index)} {...restProps}>
      {!header?.hideHeader && header.label}
      {!header?.hideHeader && sort && (
        <TableSortLabel
          active={header.name === sortOrder.name}
          IconComponent={() => (
            <DropdownIndicator active={header.name === sortOrder.name} direction={sortOrder.direction} />
          )}
          direction={header.name === sortOrder.name ? sortOrder.direction : 'asc'}
        />
      )}
    </TableCell>
  );
};

// CustomCheckbox When multi-select rows then show checkbox
const CustomCheckbox = (props: MUIDataTableCheckboxProps) => {
  const newProps = { ...props };
  newProps.color = 'primary';
  return <YBCheckbox label="" {...newProps} />;
};

// CustomRadio When single-select rows then show radio
const CustomRadio = (props: { [x: string]: string }) => {
  const newProps = Object.assign({}, props);
  newProps.color = 'primary';
  if (props['data-description'] === 'row-select') {
    return <YBRadio {...newProps} />;
  }
  return '';
};

// In case 2 row template then this will remove expand button
const ExpandButton = () => {
  const classes = useStyles();
  return <div className={classes.expandClass} />;
};

export const YBTable = ({
  title = '',
  selectableRows = 'none',
  withBorder = true,
  data,
  columns,
  options
}: YBTableProps): ReactElement => {
  const classes = useStyles();
  const overriddenOptions: MUIDataTableOptions = {
    selectableRows,
    elevation: 0,
    rowHover: false,
    filter: false,
    search: false,
    download: false,
    print: false,
    viewColumns: false,
    responsive: 'standard',
    setTableProps: () => ({ size: 'small' }),
    ...options // put it last to allow overriding default options
  };

  const cols = columns.map((col) => {
    if (col?.options) {
      col.options.customHeadRender = (
        columnMeta: ColumnMeta,
        updateDirection: (arg0: number) => void,
        sortOrder: MUISortOptions
      ) => cHeadRender(columnMeta, updateDirection, sortOrder, col.options?.setCellHeaderProps, options?.sort ?? true);
    }
    return col;
  });

  const tableContainerDiv = withBorder ? classes.root : clsx(classes.root, classes.noBorderContainer);

  return (
    <div className={tableContainerDiv}>
      <TableContainer component={Paper}>
        <MUIDataTable
          title={title}
          data={data}
          columns={cols}
          options={overriddenOptions}
          components={{
            Checkbox: selectableRows === 'single' ? CustomRadio : CustomCheckbox,
            ExpandButton
          }}
        />
      </TableContainer>
    </div>
  );
};
/* eslint-enable @typescript-eslint/ban-types */
