import React, { FC, useState } from 'react';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { useTheme } from '@material-ui/styles';
import { useTranslation } from 'react-i18next';

const useStyles = makeStyles((theme) => ({
  iconSquare: {
    borderWidth: theme.spacing(0.25),
    borderStyle: 'solid',
    width: theme.spacing(1.5),
    height: theme.spacing(1.5),
    marginRight: theme.spacing(1)
  },
  iconCircle: {
    borderWidth: theme.spacing(0.25),
    width: theme.spacing(2),
    height: theme.spacing(0.25),
    marginRight: theme.spacing(1),
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    '&:before': {
      content: '""',
      display: 'block',
      width: theme.spacing(1),
      height: theme.spacing(1),
      borderRadius: '50%',
      backgroundColor: 'inherit'
    }
  }
}));

export interface LegendItem {
  title: string;
  icon: 'square' | 'circle';
  fillColor: string;
  borderColor?: string;
}

export interface YBChartLegendProps {
  items: LegendItem[];
  isSelectable: boolean;
  onSelect?: (selectedIndex: number | null) => void;
}

export const YBChartLegend: FC<YBChartLegendProps> = ({ items, isSelectable, onSelect }) => {
  const classes = useStyles();
  const [selected, setSelected] = useState<number | null>(null);
  const handleSelectLegend = (index: number | null) => {
    if (isSelectable && onSelect && items.length > 1) {
      setSelected(index);
      onSelect(index);
    }
  };
  const { t } = useTranslation();

  const theme = useTheme();

  return (
    <Box display="flex" alignItems="center" paddingTop={2} paddingLeft={1}>
      {selected !== null && (
        <Box
          style={{ cursor: 'pointer' }}
          display="flex"
          alignItems="center"
          marginLeft={2}
          onClick={() => handleSelectLegend(null)}
        >
          <Box
            className={classes.iconCircle}
            style={{
              borderColor: theme.palette.common.black as string,
              backgroundColor: theme.palette.common.black as string
            }}
          />
          <Typography variant="subtitle1" color="textSecondary">
            {t('common.all')}
          </Typography>
        </Box>
      )}
      {items.map((item, index) => (
        <Box
          style={{ cursor: items.length > 1 ? 'pointer' : 'default' }}
          display="flex"
          alignItems="center"
          marginLeft={2}
          key={item.title}
          onClick={() => handleSelectLegend(index)}
        >
          <Box
            className={item.icon === 'square' ? classes.iconSquare : classes.iconCircle}
            style={{
              borderColor: (selected === null || selected === index
                ? item.borderColor
                : theme.palette.common.black) as string,
              backgroundColor: (selected === null || selected === index
                ? item.fillColor
                : theme.palette.common.black) as string
            }}
          />
          <Typography variant="subtitle1" color="textSecondary">
            {item.title}
          </Typography>
        </Box>
      ))}
    </Box>
  );
};
