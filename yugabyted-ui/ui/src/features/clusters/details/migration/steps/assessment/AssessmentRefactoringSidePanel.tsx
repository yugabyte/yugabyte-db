import React, { FC, useMemo } from "react";
import { Box, Divider, MenuItem, TablePagination, Typography, makeStyles } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBAccordion, YBCodeBlock, YBInput, YBModal, YBSelect, YBToggle } from "@app/components";
import SearchIcon from "@app/assets/search.svg";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import type { UnsupportedSqlWithDetails } from "@app/api/src";
import WarningIcon from "@app/assets/alert-solid.svg";
import { getMappedData } from "./refactoringUtils";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(4),
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    textTransform: "uppercase",
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
  queryCodeBlock: {
    lineHeight: 1.5,
    padding: theme.spacing(1),
  },
  toggleSwitch: {
    flexShrink: 0,
  },
  warningIcon: {
    height: "14px",
    width: "14px",
    marginBottom: "2px",
    color: theme.palette.warning[500],
  },
  warningBox: {
    marginTop: theme.spacing(2),
    background: theme.palette.warning[100],
    color: theme.palette.warning[900],
    padding: `${theme.spacing(0.6)}px ${theme.spacing(1)}px`,
    borderRadius: theme.shape.borderRadius
  }
}));

interface MigrationRefactoringSidePanelProps {
  data: UnsupportedSqlWithDetails | undefined;
  onClose: () => void;
  header: string;
  title: string;
}

export const MigrationRefactoringSidePanel: FC<MigrationRefactoringSidePanelProps> = ({
  data,
  onClose,
  header,
  title,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [page, setPage] = React.useState<number>(0);
  const [perPage, setPerPage] = React.useState<number>(5);

  const [search, setSearch] = React.useState<string>("");
  const [selectedAck, setSelectedAck] = React.useState<string>("");

  const mappedData = getMappedData(data?.suggestions_errors, "filePath")

  const filteredData = useMemo(() => {
    const searchQuery = search.toLowerCase().trim();
    return mappedData?.filter((obj) =>
      // TODO: Remove 'as any' once the ack functionality is implemented
      (selectedAck ? (obj as any).ack === (selectedAck === "Acknowledged") : true) && search
        ? obj.groupKey?.toLowerCase().includes(searchQuery) ||
          obj.sqlStatements.some(sql => sql?.toLowerCase().includes(searchQuery))
        : true
    );
  }, [data, search, selectedAck]);

  const filteredPaginatedData = useMemo(() => {
    return filteredData?.slice(page * perPage, page * perPage + perPage);
  }, [filteredData, page, perPage]);

  const totalItemCount = filteredData?.reduce((acc, obj) => acc + obj.sqlStatements.length, 0);

  return (
    <YBModal
      open={!!data}
      title={header + (data?.unsupported_type ? `: ${data.unsupported_type}` : "")}
      onClose={onClose}
      enableBackdropDismiss
      titleSeparator
      cancelLabel={t("common.close")}
      isSidePanel
    >
      <Box display="flex" alignItems="center" gridGap={10} my={2}>
        <Box flex={3}>
          <Typography variant="body1" className={classes.label}>
            {t("clusterDetail.voyager.planAndAssess.refactoring.details.search")}
          </Typography>
          <YBInput
            className={classes.fullWidth}
            placeholder={t(
              "clusterDetail.voyager.planAndAssess.refactoring.details.searchPlaceholder"
            )}
            InputProps={{
              startAdornment: <SearchIcon />,
            }}
            onChange={(ev) => setSearch(ev.target.value)}
            value={search}
          />
        </Box>
        <Box flex={1}>
          <Typography variant="body1" className={classes.label}>
            {t("clusterDetail.voyager.planAndAssess.refactoring.details.acknowledged")}
          </Typography>
          <YBSelect
            className={classes.fullWidth}
            value={selectedAck}
            onChange={(e) => setSelectedAck(e.target.value)}
          >
            <MenuItem value="">All</MenuItem>
            <Divider className={classes.divider} />
            <MenuItem value="Acknowledged">Acknowledged</MenuItem>
            <MenuItem value="Not acknowledged">Not acknowledged</MenuItem>
          </YBSelect>
        </Box>
      </Box>

      <Box>
        <YBAccordion
          titleContent={title}
          renderChips={() => (
            <YBBadge icon={false} text={`0 / ${totalItemCount} ${title}`} variant={BadgeVariant.Light} />
          )}
          graySummaryBg
          defaultExpanded
        >
          <Box display="flex" flexDirection="column" gridGap={10}>
            {filteredPaginatedData?.map(({ groupKey: filePath, sqlStatements, reasons }) => (
              <Box display="flex" flexDirection="column">
                <Box my={2}>
                  <Typography variant="body2">{filePath}</Typography>
                </Box>
                <Box display="flex" flexDirection="column" gridGap={10}>
                  {sqlStatements.map((sql, index) => (
                    <YBCodeBlock
                      text={
                        <Box display="flex" gridGap={2}>
                          <Box flex={1}>
                            {sql}
                            {reasons[index] && (
                              <Box display="flex" alignItems="center" gridGap={6} className={classes.warningBox}>
                                <WarningIcon className={classes.warningIcon} />
                                {reasons[index]}
                              </Box>
                            )}
                          </Box>
                          <YBToggle
                            className={classes.toggleSwitch}
                            label={t("clusterDetail.voyager.planAndAssess.refactoring.details.acknowledge")}
                          />
                        </Box>
                      }
                      preClassName={classes.queryCodeBlock}
                    />
                  ))}
                </Box>
              </Box>
            ))}
            <Box ml="auto">
              <TablePagination
                component="div"
                count={filteredData?.length || 0}
                page={page}
                onPageChange={(_, newPage) => setPage(newPage)}
                rowsPerPageOptions={[5, 10, 20]}
                rowsPerPage={perPage}
                onRowsPerPageChange={(e) => setPerPage(parseInt(e.target.value, 10))}
              />
            </Box>
          </Box>
        </YBAccordion>
      </Box>
    </YBModal>
  );
};
