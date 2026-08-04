import React, { FC } from "react";
import { Box, BoxProps, makeStyles } from "@material-ui/core";

export type TextBadgeProps = BoxProps;

const useStyles = makeStyles((theme) => ({
  badge: {
    backgroundColor: theme.palette.grey[200],
    color: theme.palette.grey[700],
    borderRadius: theme.shape.borderRadius,
    fontSize: "10px",
    fontWeight: 500,
    height: "fit-content",
    letterSpacing: "0.2px",
    textTransform: "uppercase",
    padding: theme.spacing(0.5, 1),
  },
}));

export const YBTextBadge: FC<TextBadgeProps> = (props) => {
  const classes = useStyles();

  return <Box className={classes.badge} {...props} />;
};
