import { VFC, ReactElement } from 'react';
import { Box, makeStyles, Theme, Tooltip, TooltipProps } from '@material-ui/core';
// import InfoSolidIcon from '@app/assets/info-solid.svg';

// make children optional and add dark style
export type YBTooltipProps = { children?: ReactElement; dark?: boolean } & Omit<
  TooltipProps,
  'children'
>;

// const useStyles = makeStyles((theme: Theme) => ({
//   icon: {
//     width: 14,
//     height: 14,
//     cursor: 'pointer',
//     color: theme.palette.grey[500]
//   }
// }));

const useDarkStyle = makeStyles((theme: Theme) => ({
  arrow: {
    '&:before': {
      backgroundColor: theme.palette.grey[900],
      border: `1px solid ${theme.palette.grey[900]}`
    }
  },
  tooltip: {
    backgroundColor: theme.palette.grey[900],
    border: `1px solid ${theme.palette.grey[900]}`,
    color: theme.palette.common.white
  }
}));

export const YBTooltip: VFC<YBTooltipProps> = ({ children, dark, ...props }) => {
  // const classes = useStyles();
  const darkClasses = useDarkStyle();
  return (
    <Tooltip
      arrow
      classes={dark ? darkClasses : undefined} // light tooltip is defined in main theme
      placement="top-start"
      {...props}
    >
      {children ?? (
        <Box lineHeight={0} ml={0.5}>
          {/* <InfoSolidIcon className={classes.icon} /> */}
        </Box>
      )}
    </Tooltip>
  );
};
