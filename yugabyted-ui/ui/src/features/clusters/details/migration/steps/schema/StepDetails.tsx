import React, { FC } from "react";
import { Box, Link, makeStyles, Paper, Typography, useTheme } from "@material-ui/core";
import StepDetailsIcon from "@app/assets/StepDetails.svg";
import BookIcon from "@app/assets/book.svg";

const useStyles = makeStyles((theme) => ({
  paper: {
    border: "1px dashed",
    borderColor: theme.palette.primary[300],
    backgroundColor: theme.palette.primary[100],
    textAlign: "center",
  },
  icon: {
    marginTop: theme.spacing(1),
    flexShrink: 0,
    height: "fit-content",
  },
  message: {
    color: theme.palette.grey[700]
  }
}));

interface StepDetailsProps {
  heading: string;
  message: string;
  docsText: string;
  docsLink: string;
}

export const StepDetails: FC<StepDetailsProps> = ({ heading, message, docsText, docsLink }) => {
  const classes = useStyles();
  const theme = useTheme();

  return (
    <Paper className={classes.paper}>
      <Box
        p={3}
        display="flex"
        flexDirection="column"
        alignItems="center"
        gridGap={theme.spacing(3)}
      >
        <StepDetailsIcon className={classes.icon} />
        <Box display="flex" flexDirection="column" alignItems="center" gridGap={theme.spacing(2.5)}>
          <Typography variant="h5">{heading}</Typography>
          <Typography variant="body2" className={classes.message}>{message}</Typography>
          <Box display="flex" gridGap={theme.spacing(1.5)} alignItems="center">
            <BookIcon />
            <Link href={docsLink} target="_blank">
              {docsText}
            </Link>
          </Box>
        </Box>
      </Box>
    </Paper>
  );
};
