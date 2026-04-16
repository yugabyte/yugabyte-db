import React, { FC, useMemo } from "react";
import {
  Box,
  Grid,
  Link,
  Paper,
  TablePagination,
  makeStyles,
} from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBCodeBlock, YBModal } from "@app/components";
import { MetadataItem } from "../../components/MetadataItem";
import type { UnsupportedObjectData } from "./AssessmentRefactoring";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(4),
  },
  label: {
    textTransform: "uppercase",
    marginBottom: theme.spacing(0.75),
    padding: 0,
    textAlign: "left",
    fontSize: '11.5px',
    fontWeight: 500,
    color: '#6D7C88',
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
  },
  linkDocs: {
    alignSelf: 'stretch',
    color: '#2B59C3',
    fontFamily: 'Inter',
    fontSize: 13,
    fontStyle: 'normal',
    fontWeight: 400,
    lineHeight: '16px',
    textDecorationLine: 'underline',
    textDecorationStyle: 'solid',
    textDecorationSkipInk: 'none',
    textDecorationThickness: 'auto',
    textUnderlineOffset: 'auto',
    textUnderlinePosition: 'from-font',
  },
}));

interface MigrationRefactoringIssueSidePanel {
  open: boolean;
  onClose: () => void;
  issue: UnsupportedObjectData | undefined;
  description?: string
}

export const MigrationRefactoringIssueSidePanel: FC<MigrationRefactoringIssueSidePanel> = ({
  open,
  onClose,
  issue,
  description
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
              {paginatedObjects?.[0].object_type && (
                <Grid item xs={3}>
                  <MetadataItem
                    layout="vertical"
                    label={t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges."
                      + "objectType")}
                    value={paginatedObjects?.[0].object_type}
                  />
                </Grid>
              )}

              {issue?.issue_name && (
                <Grid item xs={3}>
                  <MetadataItem
                    layout="vertical"
                    label={t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges."
                      + "issue")}
                    value={issue?.issue_name}
                  />
                </Grid>
              )}

              {/* {issue?.issue_type && (
                <Grid item xs={4}>
                  <Typography variant="subtitle2" className={classes.label}>
                   {t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.issueType")}
                  </Typography>
                  <Typography variant="body2" className={classes.value}>
                    {issue?.issue_type}
                  </Typography>
                </Grid>
              )} */}

              {issue?.count && (
                <Grid item xs={3}>
                  <MetadataItem
                    layout="vertical"
                    label={t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges."
                      + "occurrences")}
                    value={issue?.count}
                  />
                </Grid>
              )}
              {issue?.docs_link && (
                  <Grid item xs={3}>
                    <MetadataItem
                      layout="vertical"
                      label={t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges."
                        + "workaround")}
                      value={
                        <Link
                          href={issue?.docs_link}
                          target="_blank"
                          rel="noopener noreferrer"
                          className={classes.linkDocs}
                        >
                          {t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges."
                            + "linkToDocs")}
                        </Link>
                      }
                    />
                  </Grid>
                )
              }

              {description && (
                <Grid item xs={12}>
                  <MetadataItem
                    layout="vertical"
                    label={t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges."
                      + "description")}
                    value={description}
                  />
                </Grid>
              )}
            </Grid>
          </Box>
        </Paper>
      </Box>

      {paginatedObjects?.map(({ object_name, sql_statement }) => (
        (object_name || sql_statement) && (
          <Box key={object_name} className={classes.borderForNameAndSQL}
            sx={{ mb: 2, p: 2, borderRadius: 8 }}>
            {(object_name) && (
              <Box>
                {object_name && (
                  <MetadataItem
                    layout="vertical"
                    label={t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges." +
                      "objectName")}
                    value={object_name}
                  />
                )}
              </Box>
            )}
            {sql_statement && (
              <Box sx={{ mt: 2 }}>
                <YBCodeBlock text={sql_statement} showCopyIconButton={true}/>
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
