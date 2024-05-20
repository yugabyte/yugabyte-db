import React, { FC, useMemo } from "react";
import { Box, Divider, MenuItem, Typography, makeStyles } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBAccordion, YBCodeBlock, YBInput, YBModal, YBSelect, YBToggle } from "@app/components";
import SearchIcon from "@app/assets/search.svg";
import type { RefactoringDataItems } from "./AssessmentRefactoring";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";

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
}));

interface MigrationUnsupportedRefactoringProps {
  data: RefactoringDataItems[number] | undefined;
  onClose: () => void;
}

export const MigrationUnsupportedRefactoring: FC<MigrationUnsupportedRefactoringProps> = ({
  data,
  onClose,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [page, setPage] = React.useState<number>(1);
  const perPage = 5;

  const [search, setSearch] = React.useState<string>("");
  const [selectedAck, setSelectedAck] = React.useState<string>("");

  const filteredData = useMemo(() => {
    const searchQuery = search.toLowerCase().trim();
    return data?.objects.filter((obj) =>
      (selectedAck ? obj.ack === (selectedAck === "Acknowledged") : true) && search
        ? obj.filePath.toLowerCase().includes(searchQuery) ||
          obj.sql.toLowerCase().includes(searchQuery)
        : true
    );
  }, [data, search, selectedAck]);

  const filteredPaginatedData = useMemo(() => {
    const start = (page - 1) * perPage;
    return filteredData?.slice(start, start + perPage);
  }, [filteredData, page]);

  return (
    <YBModal
      open={!!data}
      title={t("clusterDetail.voyager.planAndAssess.refactoring.details.heading", {
        datatype: data?.datatype,
      })}
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
          titleContent={"Table"}
          renderChips={() => (
            <YBBadge icon={false} text={"0 / 1 Tables"} variant={BadgeVariant.Light} />
          )}
          defaultExpanded
        >
          <Box display="flex" flexDirection="column">
            <Box my={2}>
              <Typography variant="body2">/home/nikhil/tradex/schema/tables/table.sql</Typography>
            </Box>
            <YBCodeBlock
              text={
                <Box display="flex" gridGap={2}>
                  <Box flex={1}>
                    CREATE or REPLACE VIEW stock_trend (symbol_id, trend) AS (select trade_symbol as
                    symbol_id, JSON_ARRAYAGG(trunc(high_price, 2) order by price_time DESC) as trend
                    FROM trade_symbol_price_history tsph where interval_period = '1DAY' group by
                    trade_symbol_id);
                  </Box>
                  <YBToggle
                    className={classes.toggleSwitch}
                    label={t("clusterDetail.voyager.planAndAssess.refactoring.details.acknowledge")}
                  />
                </Box>
              }
              preClassName={classes.queryCodeBlock}
            />
            <Box mt={2} ml="auto">
              {/* <YBPagination
                currentPage={page}
                pageCount={5}
                onPageSelect={(pageNumber) => setPage(pageNumber)}
              /> */}
            </Box>
          </Box>
        </YBAccordion>
      </Box>
    </YBModal>
  );
};
