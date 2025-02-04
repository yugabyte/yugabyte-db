import React, { FC, useMemo } from "react";
import {
  Box,
  Grid,
  Link,
  Paper,
  TablePagination,
  Typography,
  makeStyles,
} from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBCodeBlock, YBModal } from "@app/components";
import type { UnsupportedSqlInfo } from "@app/api/src";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(4),
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    textTransform: "uppercase",
    marginBottom: theme.spacing(0.75),
    padding: 0,
    textAlign: "left",
  },
  dividerHorizontal: {
    width: "100%",
    marginTop: theme.spacing(2.5),
    marginBottom: theme.spacing(2.5),
  },
  value: {
    paddingTop: theme.spacing(0.36),
    textAlign: "start",
  },
  pointer: {
    cursor: "pointer",
  },
  grayBg: {
    backgroundColor: theme.palette.background.default,
    borderRadius: theme.shape.borderRadius,
  },
  fullWidth: {
    width: "100%",
  },
  divider: {
    margin: theme.spacing(1, 0, 1, 0),
  },
  borderForNameAndSQL: {
    border: "1px solid #ddd"
  }
}));

type IssueDetails = UnsupportedSqlInfo & { issue_type?: string, sql_statement?: string };

interface MigrationRefactoringIssueSidePanel {
  open: boolean;
  onClose: () => void;
  issue: IssueDetails | undefined;
}

export const MigrationRefactoringIssueSidePanel: FC<MigrationRefactoringIssueSidePanel> = ({
  open,
  onClose,
  issue,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [page, setPage] = React.useState<number>(0);
  const [perPage, setPerPage] = React.useState<number>(10);
  const paginatedObjects = useMemo(() => {
    return issue?.objects?.slice(page * perPage, page * perPage + perPage);
  }, [issue, page, perPage]);

  return (
    <YBModal
      open={open}
      title={t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.issueDetails")}
      onClose={onClose}
      enableBackdropDismiss
      titleSeparator
      cancelLabel={t("common.close")}
      isSidePanel
    >
      <Box my={2}>
        <Paper>
          <Box p={2} className={classes.grayBg} display="flex" gridGap={20}>
            <Grid container spacing={2}>
              <Grid item xs={4}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.issue")}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {issue?.unsupported_type}
                </Typography>
              </Grid>
              <Grid item xs={4}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.issueType")}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {issue?.issue_type}
                </Typography>
              </Grid>
              <Grid item xs={4}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges." +
                    "occurrences")}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {issue?.count}
                </Typography>
              </Grid>
              {issue?.docs_link ? (
                <Grid item xs={12}>
                  <Link href={issue?.docs_link} target="_blank">
                    {t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges." +
                      "linkToDocs")}
                  </Link>
                </Grid>
              ) : null}
            </Grid>
          </Box>
        </Paper>
      </Box>
      {paginatedObjects?.map(({ object_name, sql_statement }) => (
        (object_name || sql_statement) && (
          <Box key={object_name} className={classes.borderForNameAndSQL}
            sx={{ mb: 2, p: 2, borderRadius: 2 }}>
            {object_name && (
              <Typography variant="body1">
                {t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges." +
                      "objectName")} <span>{object_name}</span>
              </Typography>
            )}
            {sql_statement && (
              <Box sx={{ mt: 2 }}>
                <YBCodeBlock text={sql_statement} />
              </Box>
            )}

          </Box>
        )
      ))}
      <Box ml="auto">
        <TablePagination
          component="div"
          count={issue?.objects?.length || 0}
          page={page}
          onPageChange={(_, newPage) => setPage(newPage)}
          rowsPerPageOptions={[5, 10, 20]}
          rowsPerPage={perPage}
          onRowsPerPageChange={(e) => setPerPage(parseInt(e.target.value, 10))}
        />
      </Box>
    </YBModal>
  );
};
