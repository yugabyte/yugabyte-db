import React, { FC } from "react";
import { Box, makeStyles, Paper, Typography } from "@material-ui/core";
import type { Migration } from "./MigrationOverview";
import { STATUS_TYPES, YBStatus } from "@app/components";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(5),
  },
}));

interface MigrationImportSchemaProps {
  heading: string;
  migration: Migration;
}

export const MigrationImportSchema: FC<MigrationImportSchemaProps> = ({ heading, migration }) => {
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
            <Typography variant="h5">Imported schema</Typography>
            <Typography variant="body2">Schema has been imported successfully</Typography>
          </Box>
        </Box>
      </Box>
    </Paper>
  );
};
