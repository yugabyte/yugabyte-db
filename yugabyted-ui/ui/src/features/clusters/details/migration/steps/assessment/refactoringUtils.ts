import type { ErrorsAndSuggestionsDetails } from "@app/api/src";

export const getMappedData = (data: ErrorsAndSuggestionsDetails[] | undefined, groupKey: "objectType" | "filePath" = "objectType") => {
  // Create a map where the keys are groupKey and the values are objects containing arrays of sqlStatement, reason
  const groupMap: { [key: string]: { sqlStatements: string[], reasons: string[] } } = {};
  data?.forEach((detail) => {
    const groupKeyValue = detail[groupKey];
    if (groupKeyValue) {
      if (!groupMap[groupKeyValue]) {
        groupMap[groupKeyValue] = { sqlStatements: [], reasons: [] };
      }
      groupMap[groupKeyValue].sqlStatements.push(detail.sqlStatement || "");
      groupMap[groupKeyValue].reasons.push(detail.reason || "");
    }
  });

  // Convert the map to an array of { groupKey, sqlStatements, reasons }
  const mappedData = Object.entries(groupMap).map(([groupKey, { sqlStatements, reasons }]) => {
    return { groupKey, sqlStatements, reasons };
  });

  return mappedData
}
