import React, { FC } from "react";
import { Box, makeStyles, Paper, Typography, Link as MUILink } from "@material-ui/core";
import type { Migration } from "./MigrationOverview";
import { STATUS_TYPES, YBStatus } from "@app/components";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(5),
  },
  nextSteps: {
    paddingLeft: theme.spacing(4),
    marginTop: theme.spacing(4),
  },
  link: {
    color: "unset",
    textDecoration: "underline",
    "&:hover": {
      color: theme.palette.primary.main,
    },
  },
}));

interface MigrationVerifyProps {
  heading: string;
  migration: Migration;
}

export const MigrationVerify: FC<MigrationVerifyProps> = ({ heading, migration }) => {
  const classes = useStyles();

  return (
    <Paper>
      <Box p={4}>
        <Typography variant="h4" className={classes.heading}>
          {heading}
        </Typography>
        <Box display="flex" gridGap={4} alignItems="center">
          <YBStatus type={STATUS_TYPES.SUCCESS} size={42} />
          <Box display="flex" flexDirection="column">
            <Typography variant="h5">Migration complete</Typography>
            <Typography variant="body2">
              <MUILink
                className={classes.link}
                href={"https://docs.yugabyte.com/preview/migrate/migrate-steps/#verify-migration"}
                target="_blank"
              >
                Click here
              </MUILink>{" "}
              to learn more about the next steps
            </Typography>
          </Box>
        </Box>
        <ul className={classes.nextSteps}>
          <li>
            <Typography variant="body2">
              It is recommended to manually run validation queries on both the source and target
              database to ensure that the data is correctly migrated.
            </Typography>
          </li>
          <li>
            <Typography variant="body2">
              A sample query to validate the databases can include checking the row count of each
              table.
            </Typography>
          </li>
        </ul>
      </Box>
    </Paper>
  );
};
