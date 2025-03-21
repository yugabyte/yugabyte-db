import type { AnalysisIssueDetails } from "@app/api/src";

export const getMappedData = (data: AnalysisIssueDetails[] | undefined) => {
  const groupMap: Record<
    string,
    {
      sqlStatements: string[];
      reasons: string[];
      issueTypes: string[],
      suggestions: string[], GHs: string[], docs_links: string[]
    }
  > = {};

  data?.forEach((issue) => {
    const filePath = issue.filePath;

    if (!filePath) return;

    if (!groupMap[filePath]) {
      groupMap[filePath] = {
        sqlStatements: [], reasons: [], issueTypes: [], suggestions: [], GHs: [], docs_links: [] };
    }

    groupMap[filePath].sqlStatements.push(issue.sqlStatement || "");
    groupMap[filePath].reasons.push(issue.reason || "");
    groupMap[filePath].issueTypes.push(issue.issueType || "");
    groupMap[filePath].GHs.push(issue.GH || "");
    groupMap[filePath].docs_links.push(issue.docs_link || "");
    groupMap[filePath].suggestions.push(issue.suggestion || "");
  });

  return Object.entries(groupMap).map(([filePath, values]) => ({
    groupKey: filePath,
    ...values,
  }));
};
