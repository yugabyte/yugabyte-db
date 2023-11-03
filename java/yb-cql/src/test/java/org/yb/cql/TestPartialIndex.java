// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.cql;

import java.util.*;

import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.SimpleStatement;
import com.datastax.driver.core.Statement;

import org.yb.util.Pair;

import static org.yb.AssertionWrappers.assertTrue;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

class Write {
  public boolean predicate;
  public int refWriteIndex;
  public List<String> matchingCols;
  public List<List<String>> differingCols;
  public List<String> row;
  public String failureMsg;

  public Write(boolean predicate,
        int refWriteIndex,
        List<String> matchingCols,
        List<List<String>> differingCols,
        String failureMsg) throws Exception {
    this.predicate = predicate;
    this.refWriteIndex = refWriteIndex;
    this.matchingCols = matchingCols;
    this.differingCols = differingCols;
    this.failureMsg = failureMsg;
  }

  public void setRow(List<String> row) {
    this.row = new ArrayList<String>(row);
  }
}

@RunWith(value=YBTestRunner.class)
public class TestPartialIndex extends BaseCQLTest {

  // TODO(Piyush):
  //
  //   1. Test clustering indexes and orderby with predicates - low priority
  //   2. Add tests that delete only indexed/covered/predicate columns in
  //      testPartialIndexDeletesInternal() - high priority
  //   3. Test paging selects - high priority
  //   4. Run tests in batch mode. - high priority (after Oleg's fixes)
  //   5. Add negative tests -
  //        i) Block all funcs for now in v1 - later block only mutable functions
  //        ii) Block predicates with data types other than INT (and friends), BOOL, TEXT

  private static final Logger LOG = LoggerFactory.getLogger(TestPartialIndex.class);

  public String testTableName = "test_partial_index";
  private int pkColCnt; // Number of pk cols in table.
  private int colCnt; // Number of cols in table.
  private List<String> colNames; // pk cols first
  // some user-provided rows that satisfy the predicate
  private List<List<String>> predTrueRows;
  // some user-provided rows that don't satisfy the predicate
  private List<List<String>> predFalseRows;
  // rows from predTrueRows inserted into main table. Used for assertions.
  private List<Integer> alreadyInsertedTrueRows;
  // rows from predFalseRows inserted into main table. Used for assertions.
  private List<Integer> alreadyInsertedFalseRows;

  // For a given choice of table, index, its indexed cols, its covering cols, and its predicate,
  // the below flags are set to help decide if some test cases are possible for partial indexes
  // or not.
  //
  // Name convention: [pk => primary key cols, i => indexed cols, c => covering cols]
  // E.g.:
  // 1. same_pk_i_c_both_pred_true_false_rows means there exists two rows with same pk,
  //    indexed cols, and covering cols but one with pred=true and another with pred=false.
  //    For instance, if there is an indexed col v1 and index has predicate v1=null, then this
  //    flag should be set to false.
  // 2. same_pk_c_diff_i_multiple_pred_false_rows means there exists more than one row with
  //    same pk, covering cols but different indexed cols such that all have pred=false.

  private boolean same_pk_i_c_both_pred_true_false_rows;
  private boolean same_pk_c_diff_i_both_pred_true_false_rows;
  private boolean same_pk_i_diff_c_both_pred_true_false_rows;
  private boolean same_pk_i_c_multiple_pred_false_rows;
  private boolean same_pk_c_diff_i_multiple_pred_false_rows;
  private boolean same_pk_i_diff_c_multiple_pred_false_rows;
  private boolean same_pk_i_c_multiple_pred_true_rows;
  private boolean same_pk_c_diff_i_multiple_pred_true_rows;
  private boolean same_pk_i_diff_c_multiple_pred_true_rows;

  // For a given choice of table, index, its indexed cols, its covering cols, and its predicate,
  // the below flags are set to help decide if some test cases are possible for unique partial
  // indexes or not.
  private boolean same_i_diff_pk_multiple_pred_true_rows;
  private boolean same_i_diff_pk_both_pred_true_false_rows;

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("disable_index_backfill_for_non_txn_tables", "false");
    return flagMap;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("allow_index_table_read_write", "true");
    flagMap.put("index_backfill_upperbound_for_user_enforced_txn_duration_ms", "1000");
    flagMap.put("index_backfill_wait_for_old_txns_ms", "100");
    flagMap.put("ycql_enable_audit_log", "true"); // TODO - remove this.
    return flagMap;
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 480;
  }

  // Below three methods are taken from TestIndex.java. Inheriting test classes has its own issues,
  // so just duplicating these small functions.
  protected void createTable(String statement, boolean strongConsistency) throws Exception {
    session.execute(
        statement + (strongConsistency ? " with transactions = {'enabled' : true};" : ";"));
  }

  protected void createIndex(String statement, boolean strongConsistency) throws Exception {
    session.execute(
        statement + (strongConsistency ? ";" :
                     " with transactions = {'enabled' : false, " +
                     "'consistency_level' : 'user_enforced'};"));
  }

  protected Set<String> queryTable(String table, String columns) {
    Set<String> rows = new HashSet<String>();
    for (Row row : session.execute(String.format("select %s from %s;", columns, table))) {
      rows.add(row.toString());
    }
    return rows;
  }

  private int idxOfCol(String colName) {
    for (int i = 0; i < colNames.size(); i++) {
      if (colNames.get(i).equals(colName))
        return i;
    }
    assertTrue(false); // Something wrong.
    return -1; // To avoid compiler error.
  }

  /**
   * Get projection of row for certain cols.
   *
   * @param row list of col values in row.
   * @param projCols list of col names to be projected.
   */
  private List<String> createRowProjection(List<String> row, List<String> projCols) {
    List<String> projRow = new ArrayList<String>();
    assertTrue(row.size() == colCnt); // Only allow projection of full rows.
    for (int i = 0; i < projCols.size(); i++) {
      String col_val = row.get(idxOfCol(projCols.get(i)));
      if (col_val.startsWith("'")) col_val = col_val.substring(1); // Handling for text data type
      if (col_val.endsWith("'")) col_val = col_val.substring(0, col_val.length()-1);
      projRow.add(col_val);
    }
    return projRow;
  }

  /**
   * Check if two rows satisfy the following conditions -
   *   1. They have the same value for all columns in matchCols.
   *   2. They differ on at least 1 column for each column group in differColsList.
   *
   * E.g.:
   *   table temp(h1 int, r1 int, v1 int, v2 int, v3 int, v4 int, primary key (h1,r1));
   *   index on temp(v1,v2) include (v3,v4);
   *
   * Tow check if two rows share the same pk but differ in set of indexed cols and differ in set of
   * covering cols:
   *   matchRows(row1, row2, [h1,r1], [[v1,v2], [v3,v4]])
   *
   * @param row1
   * @param row2
   * @param matchCols list of column names to match.
   * @param differColsList list of column groups required to have different values.
   */
  private boolean matchRows(List<String> row1, List<String> row2, List<String> matchCols,
                             List<List<String>> differColsList) {
    Set<String> differingCols = new HashSet<String>();
    assertTrue(row1.size() == colCnt);
    assertTrue(row2.size() == colCnt);

    for (int i = 0; i < colCnt; i++) {
      if (matchCols.contains(colNames.get(i)))
        if (!row1.get(i).equals(row2.get(i))) return false;

      if (!row1.get(i).equals(row2.get(i)))
        differingCols.add(colNames.get(i));
    }

    for (int i = 0; i < differColsList.size(); i++) {
      List<String> differCols = differColsList.get(i);
      boolean foundDifferCol = false;
      for (int j = 0; j < differCols.size(); j++) {
        if (differingCols.contains(differCols.get(j))) {
          foundDifferCol = true;
          break;
        }
      }
      if (!foundDifferCol) return false;
    }
    return true;
  }

  private void markPredTrueRowUnused(List<String> row) {
    int j;
    for (j = 0; j<alreadyInsertedTrueRows.size(); j++) {
      if (predTrueRows.get(alreadyInsertedTrueRows.get(j)).equals(row))
        break;
    }
    assert(j<alreadyInsertedTrueRows.size());
    alreadyInsertedTrueRows.remove(j);
  }

  private void markPredFalseRowUnused(List<String> row) {
    int j;
    for (j = 0; j<alreadyInsertedFalseRows.size(); j++) {
      if (predFalseRows.get(alreadyInsertedFalseRows.get(j)).equals(row))
        break;
    }
    assert(j<alreadyInsertedFalseRows.size());
    alreadyInsertedFalseRows.remove(j);
  }

  private void markOtherPredTrueRowUnused(List<String> row) {
    List<Integer> alreadyInsertedTrueRowTemp = new ArrayList<Integer>();
    for (int j = 0; j < alreadyInsertedTrueRows.size(); j++) {
      List<String> otherRow = predTrueRows.get(alreadyInsertedTrueRows.get(j));
      if (getPk(otherRow).equals(getPk(row)) && !row.equals(otherRow)) {
        continue;
      }
      alreadyInsertedTrueRowTemp.add(alreadyInsertedTrueRows.get(j));
    }
    this.alreadyInsertedTrueRows = alreadyInsertedTrueRowTemp;
  }

  /* Pick any row from user-provided pred=true rows that hasn't been picked yet */
  private List<String> getUnusedPredTrueRow(Integer search_row_from_idx) {
    for (int i = search_row_from_idx; i < predTrueRows.size(); i++) {
      if (!alreadyInsertedTrueRows.contains(i)) {
        alreadyInsertedTrueRows.add(i);
        markOtherPredTrueRowUnused(predTrueRows.get(i));
        return predTrueRows.get(i);
      }
    }
    return new ArrayList<String>();
  }

  private List<String> getUnusedPredFalseRow(Integer search_row_from_idx) {
    for (int i = search_row_from_idx; i < predFalseRows.size(); i++) {
      if (!alreadyInsertedFalseRows.contains(i)) {
        alreadyInsertedFalseRows.add(i);
        markOtherPredTrueRowUnused(predFalseRows.get(i));
        return predFalseRows.get(i);
      }
    }
    return new ArrayList<String>();
  }

  /* Pick any row from user-provided pred=true rows that hasn't been picked yet such that it
   * matches (and differs) with the given reference row on the specified columns (and column
   * groups).
   */
  private List<String> getUnusedPredTrueRow(List<String> referenceRow, List<String> matchCols,
                                            List<List<String>> differColsList,
                                            Integer search_row_from_idx) {
    for (int i = search_row_from_idx; i < predTrueRows.size(); i++) {
      if (matchRows(predTrueRows.get(i), referenceRow, matchCols, differColsList)) {
        if (!alreadyInsertedTrueRows.contains(i)) {
          alreadyInsertedTrueRows.add(i);
          markOtherPredTrueRowUnused(predTrueRows.get(i));
          return predTrueRows.get(i);
        }
      }
    }
    return new ArrayList<String>();
  }

  private List<String> getUnusedPredFalseRow(List<String> referenceRow, List<String> matchCols,
                                             List<List<String>> differColsList,
                                             Integer search_row_from_idx) {
    for (int i = search_row_from_idx; i < predFalseRows.size(); i++) {
      if (matchRows(predFalseRows.get(i), referenceRow, matchCols, differColsList)) {
        if (!alreadyInsertedFalseRows.contains(i)) {
          alreadyInsertedFalseRows.add(i);
          markOtherPredTrueRowUnused(predFalseRows.get(i));
          return predFalseRows.get(i);
        }
      }
    }
    return new ArrayList<String>();
  }

  private List<String> getPk(List<String> row) {
    return row.subList(0, pkColCnt);
  }

  private void assertAlreadyInsertedTrueRowsPresence(List<String> projCols,
                                                    Set<String> idxTuples) {
    List<List<String>> expectedRows = new ArrayList<List<String>>();
    for (int i = 0; i < alreadyInsertedTrueRows.size(); i++)
      expectedRows.add(predTrueRows.get(alreadyInsertedTrueRows.get(i)));

    String err_msg = "expectedRows=" + expectedRows + " actualRows=" + idxTuples;

    for (int i = 0; i < alreadyInsertedTrueRows.size(); i++) {
      String row = String.join(", ",
        createRowProjection(predTrueRows.get(alreadyInsertedTrueRows.get(i)), projCols));
      assertTrue(err_msg, idxTuples.contains("Row[" + row + "]"));
    }
    assertTrue(err_msg, idxTuples.size() == alreadyInsertedTrueRows.size());
  }

  private void assertIndex(List<String> colsInIndex) {
    List<String> mangledColsInIndex = new ArrayList<String>();
    for (String col : colsInIndex) {
      mangledColsInIndex.add("\"C$_" + col + "\"");
    }

    Set<String> idxTuples = queryTable("idx", String.join(", ", mangledColsInIndex));

    assertAlreadyInsertedTrueRowsPresence(colsInIndex, idxTuples);
  }

  private Set<String> queryOutputToStringSet(Statement query) {
    Set<String> rows = new HashSet<String>();
    ResultSet rs = session.execute(query);
    for (Row row : rs) {
      rows.add(row.toString());
    }
    return rows;
  }

  private void resetTableAndIndex() {
    session.execute(String.format("truncate table %s", testTableName));
    alreadyInsertedTrueRows.clear();
    alreadyInsertedFalseRows.clear();
  }

  private boolean getRowsForWrites(List<Write> writes, List<List<String>> rows,
      int row_idx) {
    if (row_idx >= writes.size())
      return true;

    Write write = writes.get(row_idx);
    List<String> row;
    Integer search_row_from_idx = Integer.valueOf(0);
    while (true) {
      assert(search_row_from_idx < 5);
      if (write.refWriteIndex != -1) {
        assert(write.refWriteIndex < row_idx); // Can't reference anything after this write.
        if (write.predicate) {
          row = getUnusedPredTrueRow(
            writes.get(write.refWriteIndex).row,
            write.matchingCols,
            write.differingCols,
            search_row_from_idx);
        } else {
          row = getUnusedPredFalseRow(
            writes.get(write.refWriteIndex).row,
            write.matchingCols,
            write.differingCols,
            search_row_from_idx);
        }
      } else {
        row = write.predicate ? getUnusedPredTrueRow(search_row_from_idx) :
          getUnusedPredFalseRow(search_row_from_idx);
      }

      if (row.isEmpty())
        return false;

      rows.add(row);
      write.setRow(row);
      if (getRowsForWrites(writes, rows, row_idx+1))
        return true;
      else {
        if (write.predicate)
          markPredTrueRowUnused(rows.get(row_idx));
        else
          markPredFalseRowUnused(rows.get(row_idx));

        rows.remove(row_idx);
      }
      // It might happen that there doesn't exist a later sequence of rows (using given row) such
      // that given matching/differing properties. Try next row.
      search_row_from_idx = search_row_from_idx + 1;
    }
  }

  List<Pair<String, String>> testPerformWritesInternal(List<Write> writes, List<List<String>> rows,
      int combId) {
    List<Pair<String, String>> stmts = new ArrayList<Pair<String, String>>();
    for (int i = 0; i < rows.size(); i++) {
      List<String> row = rows.get(i);
      Write write = writes.get(i);
      String stmt = "";
      // If all non-pk non-static cols are NULL, then the UPDATE actually
      // wouldn't result in an insert. In that case just use an INSERT.
      boolean allNull = true;
      for (int k = pkColCnt; k < colCnt; k++) {
        if (!row.get(k).equalsIgnoreCase("null")) {
          allNull = false;
          break;
        }
      }

      if (allNull || ((((byte) combId) >> i) & 1) == 1) {
        // ith bit in combId: 1 => INSERT.
        // Though combId is of type int (signed), it is okay to use it (we wouldn't use more than
        // 2-3 LSBs for these tests).
        stmt = String.format("INSERT INTO %s(%s) VALUES (%s)",
                                    testTableName,
                                    String.join(",", colNames),
                                    String.join(",", row));
      } else {
        // ith bit in combId: 0 => UPDATE.
        List<String> whereClauseElems = new ArrayList<String>();
        for (int k = 0; k < pkColCnt; k++) {
          whereClauseElems.add(colNames.get(k) + "=" + row.get(k));
        }

        List<String> setClauseElems = new ArrayList<String>();
        for (int k = pkColCnt; k < colCnt; k++) {
          setClauseElems.add(colNames.get(k) + "=" + row.get(k));
        }

        stmt = String.format("UPDATE %s SET %s WHERE %s",
          testTableName,
          String.join(", ", setClauseElems),
          String.join(" and ", whereClauseElems));
      }

      stmts.add(new Pair<String, String>(stmt, write.failureMsg));

      if (!write.failureMsg.isEmpty())
        assert(i == writes.size() - 1); // Any test shouldn't have more writes after a failure.
    }

    return stmts;
  }

  /*
   * Helper function to test scenarios of writes performed in sequence. The writes can be
   * done either via INSERT or UPDATE.
   *
   * @param colsInIndex names of column in index table.
   * @param writes list of Write objects. Each Write object has info on the type
   *        of row to be used - its predicate value, cols that have to match
   *        with a reference row, groups of cols that have to differ with the
   *        ref row, if it should fail, and whether to use INSERT/UPDATE.
   */
  void testPerformWrites(List<String> colsInIndex, boolean strongConsistency,
                         List<Write> writes) {
    int numWrites = writes.size();
    int numCombinations = (int) Math.pow(2, numWrites);

    for (int combId = 0; combId < numCombinations; combId++) {
      List<List<String>> rows = new ArrayList<List<String>>();
      assertTrue(getRowsForWrites(writes, rows, 0)); // Add enough rows so that the test works.

      // Perform the write sequence for each combination.
      List<Pair<String, String>> stmts = testPerformWritesInternal(writes, rows, combId);

      boolean skip_index_assertion = false;
      for (int i=0; i < stmts.size(); i++) {
        if (stmts.get(i).getSecond().isEmpty()) {
          session.execute(stmts.get(i).getFirst());
        } else {
          runInvalidStmt(stmts.get(i).getFirst(), stmts.get(i).getSecond());
          skip_index_assertion = true;
        }
      }

      if (!skip_index_assertion)
        assertIndex(colsInIndex);

      // Truncate the table.
      session.execute(String.format("truncate table %s", testTableName));

      if (strongConsistency) {
        // Run in a txn
        String failureMsg = "";
        String txn = "start transaction; ";
        for (int i=0; i < stmts.size(); i++) {
          txn += stmts.get(i).getFirst() + "; ";
          if (!stmts.get(i).getSecond().isEmpty()) {
            failureMsg = stmts.get(i).getSecond();
          }
        }
        txn += "commit;";
        if (!failureMsg.isEmpty())
          runInvalidStmt(txn, failureMsg);
        else
          session.execute(txn);

        if (!skip_index_assertion)
          assertIndex(colsInIndex);
      }

      // Drop the index, truncate the table.
      resetTableAndIndex();
    }
  }

  public void testPartialIndexDeletesInternal(String predicate, List<String> indexedCols,
                                              List<String> coveringCols, boolean strongConsistency,
                                              boolean isUnique) throws Exception {
    String includeClause = "";
    if (coveringCols.size() > 0) {
      includeClause = String.format("INCLUDE (%s)", String.join(", ", coveringCols));
    }

    List<String> colsInIndex = new ArrayList<String>();
    colsInIndex.addAll(indexedCols);
    colsInIndex.remove(coveringCols); // Remove duplicates.
    colsInIndex.addAll(coveringCols);
    colsInIndex.remove(getPk(colNames)); // Remove duplicates.
    colsInIndex.addAll(getPk(colNames));

    createIndex(
      String.format("CREATE %s INDEX idx ON %s(%s) %s WHERE %s",
        isUnique ? "UNIQUE" : "", testTableName, String.join(", ", indexedCols),
        includeClause, predicate),
      strongConsistency);

    waitForReadPermsOnAllIndexes(testTableName);

    // Delete complete row.
    // --------------------
    //       ________________________________________________________________________
    //      |              | Using DELETE stmt  |  Using UPDATE with all values NULL |
    //      |--------------+--------------------+------------------------------------|
    //      |  pred=true   | Remove index entry |  Remove index entry                |
    //      |--------------+--------------------+------------------------------------|
    //      |  pred=false  | No-op              |  No-op                             |
    //      +--------------+--------------------+------------------------------------+
    //
    // "Using UPDATE with all values NULL" requires insertion using UPDATE stmt to begin with.

    // Case with pred=true. Using DELETE stmt.
    List<String> row = getUnusedPredTrueRow(Integer.valueOf(0) /* search_row_from_idx */);

    session.execute(String.format("INSERT INTO %s(%s) VALUES (%s)",
                                  testTableName,
                                  String.join(",", colNames),
                                  String.join(",", row)));

    List<String> whereClauseElems = new ArrayList<String>();
    for (int k = 0; k < pkColCnt; k++) {
      whereClauseElems.add(colNames.get(k) + "=" + row.get(k));
    }

    assertIndex(colsInIndex); // Before deletion
    session.execute("delete from " + testTableName + " where " +
      String.join(" and ", whereClauseElems));

    this.alreadyInsertedTrueRows = new ArrayList<Integer>();
    assertIndex(colsInIndex); // After deletion
    resetTableAndIndex();

    // Case with pred=false. Using DELETE stmt.
    row = getUnusedPredFalseRow(Integer.valueOf(0) /* search_row_from_idx */);

    session.execute(String.format("INSERT INTO %s(%s) VALUES (%s)",
                                  testTableName,
                                  String.join(",", colNames),
                                  String.join(",", row)));

    whereClauseElems = new ArrayList<String>();
    for (int k = 0; k < pkColCnt; k++) {
      whereClauseElems.add(colNames.get(k) + "=" + row.get(k));
    }

    assertIndex(colsInIndex); // Before deletion
    session.execute("delete from " + testTableName + " where " +
      String.join(" and ", whereClauseElems));

    this.alreadyInsertedTrueRows = new ArrayList<Integer>();
    assertIndex(colsInIndex); // After deletion
    resetTableAndIndex();

    // Case with pred=true. Using UPDATE with all values null.
    List<String> allNullSetClauseElems = new ArrayList<String>();
    for (int k = pkColCnt; k < colCnt; k++) {
      allNullSetClauseElems.add(colNames.get(k) + "= NULL");
    }

    row = getUnusedPredTrueRow(Integer.valueOf(0) /* search_row_from_idx */);
    List<String> setClauseElems = new ArrayList<String>();
    for (int k = pkColCnt; k < colCnt; k++) {
      setClauseElems.add(colNames.get(k) + "=" + row.get(k));
    }

    whereClauseElems = new ArrayList<String>();
    for (int k = 0; k < pkColCnt; k++) {
      whereClauseElems.add(colNames.get(k) + "=" + row.get(k));
    }

    session.execute(String.format("UPDATE %s SET %s WHERE %s",
                                  testTableName,
                                  String.join(", ", setClauseElems),
                                  String.join(" and ", whereClauseElems)));

    assertIndex(colsInIndex); // Before deletion
    session.execute(String.format("UPDATE %s SET %s WHERE %s ",
                                  testTableName,
                                  String.join(", ", allNullSetClauseElems),
                                  String.join(" and ", whereClauseElems)));

    this.alreadyInsertedTrueRows = new ArrayList<Integer>();
    assertIndex(colsInIndex); // After deletion
    resetTableAndIndex();

    // Case with pred=false. Using UPDATE with all values null.
    row = getUnusedPredFalseRow(Integer.valueOf(0) /* search_row_from_idx */);

    setClauseElems = new ArrayList<String>();
    for (int k = pkColCnt; k < colCnt; k++) {
      setClauseElems.add(colNames.get(k) + "=" + row.get(k));
    }

    whereClauseElems = new ArrayList<String>();
    for (int k = 0; k < pkColCnt; k++) {
      whereClauseElems.add(colNames.get(k) + "=" + row.get(k));
    }

    session.execute(String.format("UPDATE %s SET %s WHERE %s",
                                  testTableName,
                                  String.join(", ", setClauseElems),
                                  String.join(" and ", whereClauseElems)));

    assertIndex(colsInIndex); // Before deletion
    session.execute(String.format("UPDATE %s SET %s WHERE %s ",
                                  testTableName,
                                  String.join(", ", allNullSetClauseElems),
                                  String.join(" and ", whereClauseElems)));

    this.alreadyInsertedTrueRows = new ArrayList<Integer>();
    assertIndex(colsInIndex); // After deletion
    resetTableAndIndex();
  }

  /**
   * The most important internal method to test partial index writes for a specific choice of
   * predicate, indexed columns and covering columns. This method exhaustively tests INSERT/UPDATE
   * (see the matrices in the function for each detailed case) for the combination of predicate,
   * indexed cols and covering cols provided.
   *
   * Note that we require the caller to specify required object variables (like some pred=true/false
   * rows, some properties of the specific combination i.e., the same_pk* flags, etc) before calling
   * this because it is a hard problem to generate rows that satisfy predicates and decipher
   * properties of a combination. Instead it is easier for a human to give all this information.
   *
   * The following test cases are included in this -
   *
   *  1. Insert (semantically; not talking about INSERT statement i.e., write a row with pk
   *             that doesn't exist in table)
   *    - 2 test-cases for non-unique indexes
   *    - 4 test-cases for unique indexes
   *
   *  2. Update (semantically; not talking about UPDATE statement i.e., write a row with pk
   *             that already exists in table)
   *    - 12 test-cases for non-unique indexes
   *    - 2 test-cases for unique indexes
   *
   * All of the above test-cases involve a sequence of writes which can be performed by using
   * either -
   *      a) INSERT statement.
   *      b) UPDATE statement.
   *
   * So each test case with n writes is internally executed 2^n times with different combinations
   * of INSERT/UPDATE statements.
   *
   * @param predicate
   * @param indexedCols the columns which are to be indexed.
   * @param coveringCols the columns to be covered.
   * @param strongConsistency
   * @param isUnique test on a unique index
   */
  public void testPartialIndexWritesInternal(
      String predicate, List<String> indexedCols, List<String> coveringCols,
      boolean strongConsistency, boolean isUnique) throws Exception {

    String includeClause = "";
    if (coveringCols.size() > 0) {
      includeClause = String.format("INCLUDE (%s)", String.join(", ", coveringCols));
    }

    List<String> colsInIndex = new ArrayList<String>();
    colsInIndex.addAll(indexedCols);
    colsInIndex.remove(coveringCols); // Remove duplicates.
    colsInIndex.addAll(coveringCols);
    colsInIndex.remove(getPk(colNames)); // Remove duplicates.
    colsInIndex.addAll(getPk(colNames));

    createIndex(
      String.format("CREATE %s INDEX idx ON %s(%s) %s WHERE %s",
        isUnique ? "UNIQUE" : "", testTableName, String.join(", ", indexedCols),
        includeClause, predicate),
      strongConsistency);

    waitForReadPermsOnAllIndexes(testTableName);

    // Insert (No existing row with same pk)
    // --------------------------------------
    //       ______________________________________________
    //      |New row's pred|                               |
    //      |--------------+-------------------------------|
    //      |  pred=true   |   Insert into Partial Index   |
    //      |--------------+-------------------------------|
    //      |  pred=false  |    No-op                      |
    //      +--------------+-------------------------------+

    // Case with pred=true.
    testPerformWrites(colsInIndex,
      strongConsistency,
      Arrays.asList(
        new Write(
          true, /* predicate */
          -1, /* refWriteIndex */
          new ArrayList<String>(), /* matchingCols */
          new ArrayList<List<String>>(), /* differingColsList */
          "" /* failureMsg */)
      )
    );

    // Case with pred=false.
    testPerformWrites(colsInIndex,
      strongConsistency,
      Arrays.asList(
        new Write(
          false, /* predicate */
          -1, /* refWriteIndex */
          new ArrayList<String>(), /* matchingCols */
          new ArrayList<List<String>>(), /* differingColsList */
          "" /* failureMsg */)
      )
    );

    // Insert - extra cases applicable only to unique partial indexes.
    // ---------------------------------------------------------------
    //      _______________________________________________________________________________________
    //     |               |  Table has row with same indexed  |  Table has no row with same       |
    //     |New row's pred |  col values and pred=true         |  indexed col values and pred=true |
    //     |---------------|-----------------------------------|-----------------------------------|
    //     | pred=true     |   FAIL OP                         |     Insert into UPI               |
    //     |---------------+-----------------------------------+-----------------------------------|
    //     | pred=false    |   No-op                           |     No-op                         |
    //     ----------------------------------------------------+------------------------------------

    // pred=true, Exists a row with same index col values and pred=true
    if (isUnique && this.same_i_diff_pk_multiple_pred_true_rows) {
      testPerformWrites(colsInIndex,
        strongConsistency,
        Arrays.asList(
          new Write(
            true, /* predicate */
            -1, /* refWriteIndex */
            new ArrayList<String>(), /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */),
          new Write(
            true, /* predicate */
            0, /* refWriteIndex */
            indexedCols, /* matchingCols */
            Arrays.asList(new ArrayList<String>(getPk(colNames))), /* differingColsList */
            "Execution Error. Duplicate value disallowed by unique index idx" /* failureMsg */)
        )
      );
    }

    // pred=false, Exists a row with same index col values and pred=true
    if (isUnique && this.same_i_diff_pk_both_pred_true_false_rows) {
      testPerformWrites(colsInIndex,
        strongConsistency,
        Arrays.asList(
          new Write(
            true, /* predicate */
            -1, /* refWriteIndex */
            new ArrayList<String>(), /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */),
          new Write(
            false, /* predicate */
            0, /* refWriteIndex */
            indexedCols, /* matchingCols */
            Arrays.asList(new ArrayList<String>(getPk(colNames))), /* differingColsList */
            "" /* failureMsg */)
        )
      );
    }

    // Update (There is an existing row with same pk)
    // ----------------------------------------------
    //
    //                                      _________________________________________________
    //                                      | Table has same pk row | Table has same pk row |
    //                                      | with pred=false       | with pred=true        |
    //     |--------------------------------|-----------------------|-----------------------|
    //     | pred=true (Same I && C cols)   | Insert into PI        | No-op                 |
    //     |--------------------------------+-----------------------+-----------------------|
    //     | pred=true (Diff I || C cols)   | Insert into PI        | Update PI             |
    //     |--------------------------------+-----------------------+-----------------------|
    //     | pred=false (Same I && C cols)  | No-op                 | Delete from PI        |
    //     |--------------------------------+-----------------------+-----------------------|
    //     | pred=false (Diff I || C cols)  | No-op                 | Delete from PI        |
    //     |--------------------------------------------------------------------------------|

    // pred=true (Same I && C cols), Same pk row exists with pred=false.
    if (this.same_pk_i_c_both_pred_true_false_rows) {
      List<String> matchingCols = new ArrayList<String>(getPk(colNames));
      matchingCols.addAll(indexedCols);
      matchingCols.addAll(coveringCols);
      testPerformWrites(colsInIndex,
        strongConsistency,
        Arrays.asList(
          new Write(
            false, /* predicate */
            -1, /* refWriteIndex */
            new ArrayList<String>(), /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */),
          new Write(
            true, /* predicate */
            0, /* refWriteIndex */
            matchingCols, /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */)
        )
      );
    }

    // pred=true (Diff I || C cols), Same pk row exists with pred=false.
    //   1. Diff I col
    //   2. Diff C col

    // Case 1
    if (this.same_pk_c_diff_i_both_pred_true_false_rows) {
      List<String> matchingCols = new ArrayList<String>(getPk(colNames));
      matchingCols.addAll(coveringCols);

      testPerformWrites(colsInIndex,
        strongConsistency,
        Arrays.asList(
          new Write(
            false, /* predicate */
            -1, /* refWriteIndex */
            new ArrayList<String>(), /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */),
          new Write(
            true, /* predicate */
            0, /* refWriteIndex */
            matchingCols, /* matchingCols */
            Arrays.asList(new ArrayList<String>(indexedCols)), /* differingColsList */
            "" /* failureMsg */)
        )
      );
    }

    // Case 2
    if (!coveringCols.isEmpty() && this.same_pk_i_diff_c_both_pred_true_false_rows) {
      List<String> matchingCols = new ArrayList<String>(getPk(colNames));
      matchingCols.addAll(indexedCols);

      testPerformWrites(colsInIndex,
        strongConsistency,
        Arrays.asList(
          new Write(
            false, /* predicate */
            -1, /* refWriteIndex */
            new ArrayList<String>(), /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */),
          new Write(
            true, /* predicate */
            0, /* refWriteIndex */
            matchingCols, /* matchingCols */
            Arrays.asList(new ArrayList<String>(coveringCols)), /* differingColsList */
            "" /* failureMsg */)
        )
      );
    }

    // pred=false (Same I && C cols), Same pk row exists with pred=false.
    if (this.same_pk_i_c_multiple_pred_false_rows) {
      List<String> matchingCols = new ArrayList<String>(getPk(colNames));
      matchingCols.addAll(indexedCols);
      matchingCols.addAll(coveringCols);

      testPerformWrites(colsInIndex,
        strongConsistency,
        Arrays.asList(
          new Write(
            false, /* predicate */
            -1, /* refWriteIndex */
            new ArrayList<String>(), /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */),
          new Write(
            false, /* predicate */
            0, /* refWriteIndex */
            matchingCols, /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */)
        )
      );
    }

    // pred=false (Diff I || C cols), Same pk row exists with pred=false.
    //   1. Diff I
    //   2. Diff C

    // Case 1
    if (this.same_pk_c_diff_i_multiple_pred_false_rows) {
      List<String> matchingCols = new ArrayList<String>(getPk(colNames));
      matchingCols.addAll(coveringCols);

      testPerformWrites(colsInIndex,
        strongConsistency,
        Arrays.asList(
          new Write(
            false, /* predicate */
            -1, /* refWriteIndex */
            new ArrayList<String>(), /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */),
          new Write(
            false, /* predicate */
            0, /* refWriteIndex */
            matchingCols, /* matchingCols */
            Arrays.asList(new ArrayList<String>(indexedCols)), /* differingColsList */
            "" /* failureMsg */)
        )
      );
    }

    // Case 2
    if (!coveringCols.isEmpty() && this.same_pk_i_diff_c_multiple_pred_false_rows) {
      List<String> matchingCols = new ArrayList<String>(getPk(colNames));
      matchingCols.addAll(indexedCols);

      testPerformWrites(colsInIndex,
        strongConsistency,
        Arrays.asList(
          new Write(
            false, /* predicate */
            -1, /* refWriteIndex */
            new ArrayList<String>(), /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */),
          new Write(
            false, /* predicate */
            0, /* refWriteIndex */
            matchingCols, /* matchingCols */
            Arrays.asList(new ArrayList<String>(coveringCols)), /* differingColsList */
            "" /* failureMsg */)
        )
      );
    }

    // pred=true (Same I && C cols), Same pk row exists with pred=true.
    if (this.same_pk_i_c_multiple_pred_true_rows) {
      List<String> matchingCols = new ArrayList<String>(getPk(colNames));
      matchingCols.addAll(indexedCols);
      matchingCols.addAll(coveringCols);

      testPerformWrites(colsInIndex,
        strongConsistency,
        Arrays.asList(
          new Write(
            true, /* predicate */
            -1, /* refWriteIndex */
            new ArrayList<String>(), /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */),
          new Write(
            true, /* predicate */
            0, /* refWriteIndex */
            matchingCols, /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */)
        )
      );
    }

    // pred=true (Diff I || C cols), Same pk row exists with pred=true.
    //   1. Diff I
    //   2. Diff C

    // Case 1
    if (this.same_pk_c_diff_i_multiple_pred_true_rows) {
      List<String> matchingCols = new ArrayList<String>(getPk(colNames));
      matchingCols.addAll(coveringCols);

      testPerformWrites(colsInIndex,
        strongConsistency,
        Arrays.asList(
          new Write(
            true, /* predicate */
            -1, /* refWriteIndex */
            new ArrayList<String>(), /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */),
          new Write(
            true, /* predicate */
            0, /* refWriteIndex */
            matchingCols, /* matchingCols */
            Arrays.asList(new ArrayList<String>(indexedCols)), /* differingColsList */
            "" /* failureMsg */)
        )
      );
    }

    // Case 2
    if (!coveringCols.isEmpty() && this.same_pk_i_diff_c_multiple_pred_true_rows) {
      List<String> matchingCols = new ArrayList<String>(getPk(colNames));
      matchingCols.addAll(indexedCols);

      testPerformWrites(colsInIndex,
        strongConsistency,
        Arrays.asList(
          new Write(
            true, /* predicate */
            -1, /* refWriteIndex */
            new ArrayList<String>(), /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */),
          new Write(
            true, /* predicate */
            0, /* refWriteIndex */
            matchingCols, /* matchingCols */
            Arrays.asList(new ArrayList<String>(coveringCols)), /* differingColsList */
            "" /* failureMsg */)
        )
      );
    }

    // pred=false (Same I && C cols), Same pk row exists with pred=true.
    if (this.same_pk_i_c_both_pred_true_false_rows) {
      testPerformWrites(colsInIndex,
        strongConsistency,
        Arrays.asList(
          new Write(
            true, /* predicate */
            -1, /* refWriteIndex */
            new ArrayList<String>(), /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */),
          new Write(
            false, /* predicate */
            0, /* refWriteIndex */
            getPk(colNames), /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */)
        )
      );
    }

    // pred=false (Diff I || C cols), Same pk row exists with pred=true.
    //   1. Diff I
    //   2. Diff C

    // Case 1
    if (this.same_pk_c_diff_i_both_pred_true_false_rows) {
      List<String> matchingCols = new ArrayList<String>(getPk(colNames));
      matchingCols.addAll(coveringCols);

      testPerformWrites(colsInIndex,
        strongConsistency,
        Arrays.asList(
          new Write(
            true, /* predicate */
            -1, /* refWriteIndex */
            new ArrayList<String>(), /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */),
          new Write(
            false, /* predicate */
            0, /* refWriteIndex */
            matchingCols, /* matchingCols */
            Arrays.asList(new ArrayList<String>(indexedCols)), /* differingColsList */
            "" /* failureMsg */)
        )
      );
    }

    // Case 2:
    if (!coveringCols.isEmpty() && this.same_pk_i_diff_c_both_pred_true_false_rows) {
      List<String> matchingCols = new ArrayList<String>(getPk(colNames));
      matchingCols.addAll(indexedCols);

      testPerformWrites(colsInIndex,
        strongConsistency,
        Arrays.asList(
          new Write(
            true, /* predicate */
            -1, /* refWriteIndex */
            new ArrayList<String>(), /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */),
          new Write(
            false, /* predicate */
            0, /* refWriteIndex */
            matchingCols, /* matchingCols */
            Arrays.asList(new ArrayList<String>(coveringCols)), /* differingColsList */
            "" /* failureMsg */)
        )
      );
    }

    // Update - extra cases applicable only to unique partial indexes.
    // ---------------------------------------------------------------
    //                   ___________________________________________________________________
    //                  | Table has a row with diff pk, same indexed columns, with pred=true|
    //                  | and another row with same pk with pred=false                      |
    //     |------------|-------------------------------------------------------------------|
    //     | pred=true  |    FAIL OP                                                        |
    //     |------------+-------------------------------------------------------------------|
    //     | pred=false |    No-op                                                          |
    //     |--------------------------------------------------------------------------------|

    if (isUnique && this.same_i_diff_pk_multiple_pred_true_rows) { // For the second existing row
      // and new row
      testPerformWrites(colsInIndex,
        strongConsistency,
        Arrays.asList(
          new Write(
            true, /* predicate */
            -1, /* refWriteIndex */
            new ArrayList<String>(), /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "" /* failureMsg */),
          new Write(
            false, /* predicate */
            0, /* refWriteIndex */
            new ArrayList<String>(), /* matchingCols */
            Arrays.asList(getPk(colNames)), /* differingColsList */
            "" /* failureMsg */),
          new Write(
            true, /* predicate */
            1, /* refWriteIndex */
            getPk(colNames), /* matchingCols */
            new ArrayList<List<String>>(), /* differingColsList */
            "Execution Error. Duplicate value disallowed by unique index idx" /* failureMsg */)
        )
      );
    }

    session.execute("drop index idx");
  }

  /**
   * Internal method to test access method selection policy.
   *
   * @param whereClause WHERE clause to test.
   * @param whereClauseWithoutIndexes WHERE clause which gives the same rows but can be used with
   *   the primary index. This is needed in cases where the where clause with partial index isn't
   *   allowed with a normal primary index scan. E.g: the = and != operator aren't allowed on the
   *   same column.
   * @param selectCols expressions to be selected in SELECT statement.
   * @param predicate1 predicate of 1st index.
   * @param indexedHashCols1 hash index cols of 1st index.
   * @param indexedRangeCols1 range index cols of 1st index.
   * @param coveringCols1 covering cols of 1st index.
   * @param addExtraIndex true if testcase requires creation of a 2nd index.
   * @param predicate2
   * @param indexedHashCols2
   * @param indexedRangeCols2
   * @param coveringCols2
   * @param strongConsistency type of consistency for index.
   * @param expectedAccessMethod access method expected in query plan.
   * @param expectedKeyConditions key conditions expected in query plan.
   * @param expectedFilterConditions filter conditions expected in query plan.
   * @param rowToInsert list of rows to insert before testing for SELECT statement.
   */
  public void testPartialIndexSelectInternal(String whereClause, String whereClauseWithoutIndexes,
      List<Object> bindValues, List<String> selectCols, String predicate1,
      List<String> indexedHashCols1, List<String> indexedRangeCols1, List<String> coveringCols1,
      boolean addExtraIndex, String predicate2, List<String> indexedHashCols2,
      List<String> indexedRangeCols2, List<String> coveringCols2, boolean strongConsistency,
      String expectedAccessMethod, String expectedKeyConditions, String expectedFilterConditions,
      List<String> rowToInsert) throws Exception {

    // Inserting rows before index creation to test index backfill as well.
    for (int i = 0; i < rowToInsert.size(); i++)
      session.execute(
        String.format("INSERT INTO %s(%s) values (%s)", testTableName,
          String.join(", ", colNames), rowToInsert.get(i)));

    String includeClause1 = "";
    if (coveringCols1.size() > 0) {
      includeClause1 = String.format("INCLUDE (%s)", String.join(", ", coveringCols1));
    }

    String indexColsStr = "("+String.join(", ", indexedHashCols1)+")";
    if (indexedRangeCols1.size() > 0)
      indexColsStr += String.join(", ", indexedRangeCols1);

    createIndex(
      String.format("CREATE INDEX idx ON %s(%s) %s WHERE %s",
        testTableName, indexColsStr,
        includeClause1, predicate1),
      strongConsistency);

    if (addExtraIndex) {
      String includeClause2 = "";
      if (coveringCols2.size() > 0) {
        includeClause2 = String.format("INCLUDE (%s)", String.join(", ", coveringCols2));
      }

      indexColsStr = "("+String.join(", ", indexedHashCols2)+")";
      if (indexedRangeCols2.size() > 0)
        indexColsStr += String.join(", ", indexedRangeCols2);
      String predicateStr2 = "";
      if (!predicate2.isEmpty()) predicateStr2 = "WHERE " + predicate2;
      createIndex(
        String.format("CREATE INDEX idx2 ON %s(%s) %s %s",
          testTableName, indexColsStr,
          includeClause2, predicateStr2),
        strongConsistency);
    }

    waitForReadPermsOnAllIndexes(testTableName);

    String query = String.format("select %s from %s where %s",
      String.join(",", selectCols), testTableName, whereClause);
    String queryWithoutIndexes = String.format("select %s from %s where %s",
      String.join(",", selectCols), testTableName, whereClauseWithoutIndexes);

    // We just execute three SELECT statements to get metadata cache of all tservers on the same
    // schema version post the index creation. If that doesn't happen we might choose a wrong
    // index via semantic analysis on a tserver which doesn't have the latest table schema and
    // hence doesn't know about the new index.
    String flat_query = query;
    for (Object bind_value : bindValues) {
      flat_query = flat_query.replaceFirst("\\?", bind_value.toString());
    }

    session.execute(flat_query);
    session.execute(flat_query);
    session.execute(flat_query);

    assertTrue(doesQueryPlanContainSubstring(query, expectedAccessMethod));

    if (!expectedKeyConditions.isEmpty())
      assertTrue(
        doesQueryPlanContainSubstring(query, expectedKeyConditions));
    else
      assertTrue(
        !doesQueryPlanContainSubstring(query, "Key Conditions:"));

    if (!expectedFilterConditions.isEmpty())
      assertTrue(
        doesQueryPlanContainSubstring(query, expectedFilterConditions));
    else
      assertTrue(
        !doesQueryPlanContainSubstring(query, "Filter:"));

    Statement stmt;
    if (whereClause.contains("?")) {
      PreparedStatement selectPreparedStmt = session.prepare(query);
      stmt = selectPreparedStmt.bind(bindValues.toArray());
    } else {
      stmt = new SimpleStatement(query);
    }

    Set<String> queryOutputWithIndexes = queryOutputToStringSet(stmt);
    session.execute("drop index idx");
    if (addExtraIndex)
      session.execute("drop index idx2");

    // Query output with expected access method should match that of the main table scan/lookup.
    String flat_query_without_indexes = queryWithoutIndexes;
    for (Object bind_value : bindValues) {
      flat_query_without_indexes =
        flat_query_without_indexes.replaceFirst("\\?", bind_value.toString());
    }
    assertTrue(queryOutputWithIndexes.toString(),
      queryOutputToStringSet(new SimpleStatement(flat_query_without_indexes)).equals(
        queryOutputWithIndexes));

    resetTableAndIndex();
  }

  @Test
  public void testPartialIndexSelectionPolicy() throws Exception {
    createTable(
      String.format("create table %s " +
        "(h1 int, h2 int, r1 int, r2 int, v1 int, v2 int, " +
        "primary key ((h1, h2), r1, r2))", testTableName),
      true /* strongConsistency */);

    pkColCnt = 4;
    colCnt = 6;
    colNames = Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"); // pk cols first

    this.alreadyInsertedTrueRows = new ArrayList<Integer>();
    this.alreadyInsertedFalseRows = new ArrayList<Integer>();

    // Test choice between partial index and main table -
    // --------------------------------------------------
    //
    //    1. Test case where WHERE clause doesn't imply partial index. Then the index will never be
    //    chosen no matter what good properties it has - such as it being covering and not requiring
    //    a full scan.
    //
    //    2. Testing cases where WHERE clause => Partial index
    //
    //      4 options for type of (main table scan, index table scan) -
    //        - (full, full scan): Choose partial index
    //        - (full, non-full scan): Choose partial index. Trivial case not tested.
    //        - (non-full, non-full scan): Choose partial index.
    //        - (non-full, full scan): Choose main table.
    //
    //    3. Test PREPAREd statement.
    //    4. Test predicate of the form: v1 = ? and v1 != NULL. This is allowed if a partial
    //       index that subsumes (v1 != NULL) exists and is chosen. Without the partial index
    //       both = and != operators on the same column are not allowed.

    // 1. Main table scan: full scan
    // 2. Partial Index: WHERE clause doesn't imply Idx predicate && requires non-full scan && is
    //    covering idx
    // [Main table is chosen]
    testPartialIndexSelectInternal(
      "v1 = 1 and r1 = 1", /* whereClause */
      "v1 = 1 and r1 = 1", /* whereClauseWithoutIndexes */
      Arrays.asList(), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"), /* selectCols */
      "v2 = NULL", /* predicate1 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList("v2"), /* coveringCols1 */
      false,  /* addExtraIndex */
      "", /* predicate2 */
      Arrays.asList(), /* indexedHashCols2 */
      Arrays.asList(), /* indexedRangeCols2 */
      Arrays.asList(), /* coveringCols2 */
      true, /* strongConsistency */
      String.format("Seq Scan on %s.%s", DEFAULT_TEST_KEYSPACE,
        testTableName), /* expectedAccessMethod */
      "", /* expectedKeyConditions */
      "Filter: (v1 = 1) AND (r1 = 1)" /* expectedFilterConditions */,
      Arrays.asList("1, 1, 1, 1, 1, 1", "2, 1, 1, 1, 2, 1") /* rowsToInsert */);

    // 1. Main table scan: full scan
    // 2. Partial Index: WHERE clause => Idx predicate && full scan
    // [Partial Index is chosen]
    testPartialIndexSelectInternal(
      "h1 = 1 and v1 = NULL", /* whereClause */
      "h1 = 1 and v1 = NULL", /* whereClauseWithoutIndexes */
      Arrays.asList(), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"), /* selectCols */
      "v1 = NULL", /* predicate1 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList(), /* coveringCols1 */
      false,  /* addExtraIndex */
      "", /* predicate2 */
      Arrays.asList(), /* indexedHashCols2 */
      Arrays.asList(), /* indexedRangeCols2 */
      Arrays.asList(), /* coveringCols2 */
      true, /* strongConsistency */
      String.format("Index Scan using %s.idx", DEFAULT_TEST_KEYSPACE), /* expectedAccessMethod */
      "", /* expectedKeyConditions */
      "Filter: (h1 = 1)", /* expectedFilterConditions */
      Arrays.asList( /* rowsToInsert */
        "1, 1, 1, 1, NULL, 1", // pred=true row and where clause satisfied
        "2, 1, 1, 1, NULL, 1", // pred=true row but where clause not satisfied
        "2, 2, 1, 1, 1, 1" // pred=false
      )
    );

    // 1. Main table scan: Non-full scan
    // 2. Partial Index: WHERE clause => Idx predicate && non-full scan
    //    && WHERE clause col ops len > predicate len.
    // [Partial Index is chosen]
    testPartialIndexSelectInternal(
      "v1 = NULL and r1 = 1 and h1 = 1 and h2 = 1", /* whereClause */
      "v1 = NULL and r1 = 1 and h1 = 1 and h2 = 1", /* whereClauseWithoutIndexes */
      Arrays.asList(), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"), /* selectCols */
      "v1 = NULL", /* predicate1 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList(), /* coveringCols1 */
      false,  /* addExtraIndex */
      "", /* predicate2 */
      Arrays.asList(), /* indexedHashCols2 */
      Arrays.asList(), /* indexedRangeCols2 */
      Arrays.asList(), /* coveringCols2 */
      true, /* strongConsistency */
      String.format("Index Scan using %s.idx", DEFAULT_TEST_KEYSPACE), /* expectedAccessMethod */
      "Key Conditions: (v1 = NULL) AND (r1 = 1)", /* expectedKeyConditions */
      "Filter: (h1 = 1) AND (h2 = 1)", /* expectedFilterConditions */
      Arrays.asList( /* rowsToInsert */
        "1, 1, 1, 1, NULL, 1", // pred=true row and where clause satisfied
        "2, 1, 1, 1, NULL, 1", // pred=true row but where clause not satisfied
        "2, 2, 1, 1, 1, 1" // pred=false
      )
    );

    // 1. Main table scan: Non-full scan
    // 2. Partial Index: WHERE clause => Idx predicate && full scan
    // [Main table is chosen]
    testPartialIndexSelectInternal(
      "h1 = 1 and h2 = 1 and v1 = NULL", /* whereClause */
      "h1 = 1 and h2 = 1 and v1 = NULL", /* whereClauseWithoutIndexes */
      Arrays.asList(), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"), /* selectCols */
      "v1 = NULL", /* predicate1 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList(), /* coveringCols1 */
      false,  /* addExtraIndex */
      "", /* predicate2 */
      Arrays.asList(), /* indexedHashCols1 */
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList(), /* coveringCols2 */
      true, /* strongConsistency */
      String.format("Range Scan on %s.%s", DEFAULT_TEST_KEYSPACE,
        testTableName), /* expectedAccessMethod */
      "Key Conditions: (h1 = 1) AND (h2 = 1)", /* expectedKeyConditions */
      "Filter: (v1 = NULL)", /* expectedFilterConditions */
      Arrays.asList( /* rowsToInsert */
        "1, 1, 1, 1, NULL, 1", // where clause satisfied
        "2, 1, 1, 1, NULL, 1", // where clause not satisfied
        "2, 2, 1, 1, 1, 1" // where clause not satisfied
      )
    );

    // Test PREPAREd statement.
    // 1. Main table scan: full scan
    // 2. Partial Index: WHERE clause => Idx predicate && full scan
    // [Partial Index is chosen]
    testPartialIndexSelectInternal(
      "h1 = ? and v1 = NULL", /* whereClause */
      "h1 = ? and v1 = NULL", /* whereClauseWithoutIndexes */
      Arrays.asList(Integer.valueOf(1)), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"), /* selectCols */
      "v1 = NULL", /* predicate1 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList(), /* coveringCols1 */
      false,  /* addExtraIndex */
      "", /* predicate2 */
      Arrays.asList(), /* indexedHashCols2 */
      Arrays.asList(), /* indexedRangeCols2 */
      Arrays.asList(), /* coveringCols2 */
      true, /* strongConsistency */
      String.format("Index Scan using %s.idx", DEFAULT_TEST_KEYSPACE), /* expectedAccessMethod */
      "", /* expectedKeyConditions */
      "Filter: (h1 = :h1)", /* expectedFilterConditions */
      Arrays.asList( /* rowsToInsert */
        "1, 1, 1, 1, NULL, 1", // pred=true row and where clause satisfied
        "2, 1, 1, 1, NULL, 1", // pred=true row but where clause not satisfied
        "2, 2, 1, 1, 1, 1" // pred=false
      )
    );

    // Test = and != operator on the same column in a query.
    //
    // 1. Main table scan: full scan
    // 2. Partial Index: WHERE clause => Idx predicate && full scan
    // [Partial Index is chosen]
    testPartialIndexSelectInternal(
      "v1 = ? and v1 != NULL", /* whereClause */
      "v1 = ?", /* whereClauseWithoutIndexes */ // Assuming that we don't bind with NULL
      Arrays.asList(Integer.valueOf(1)), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"), /* selectCols */
      "v1 != NULL", /* predicate1 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList(), /* coveringCols1 */
      false,  /* addExtraIndex */
      "", /* predicate2 */
      Arrays.asList(), /* indexedHashCols2 */
      Arrays.asList(), /* indexedRangeCols2 */
      Arrays.asList(), /* coveringCols2 */
      true, /* strongConsistency */
      String.format("Index Scan using %s.idx", DEFAULT_TEST_KEYSPACE), /* expectedAccessMethod */
      "", /* expectedKeyConditions */
      "Filter: (v1 = :v1)", /* expectedFilterConditions */
      Arrays.asList( /* rowsToInsert */
        "1, 1, 1, 1, 1, 1", // pred=true row and where clause satisfied
        "2, 1, 1, 1, 2, 1", // pred=true row but where clause not satisfied
        "2, 2, 1, 1, NULL, 1" // pred=false
      )
    );

    // Test choice between 2 partial indexes -
    // ---------------------------------------
    //  Consider only cases where both indexes satisfy WHERE clause => idx predicate condition.
    //  If an index doesn't satisfy, it is not even considered, this is already tested above.
    //
    //    If both indexes require full scans, test 2 cases -
    //      - One which has longer predicate len wins
    //      - If they have same predicate len, they follow non-partial index
    //        policies (not testing here).
    //
    //    If both indexes require non-full scans, test 2 cases -
    //      - One which has longer predicate len wins
    //      - If they have same predicate len, they follow non-partial index
    //        policies (not testing here).
    //
    //    If one index requires full scan and another requires non-full scan choose
    //    the one with non-full scan always. Test this case only -
    //      - Idx1 has a longer predicate, but Idx1 requires full scan and
    //        Idx2 doesn't require full scan.

    // 1. Partial Index 1: WHERE clause => Idx predicate && full scan
    // 2. Partial Index 2: WHERE clause => Idx predicate && full scan
    //     Idx2 has longer predicate.
    // 3. Main table: Full scan
    // [Idx2 is chosen]
    testPartialIndexSelectInternal(
      "h1 = 1 and v1 = NULL", /* whereClause */
      "h1 = 1 and v1 = NULL", /* whereClauseWithoutIndexes */
      Arrays.asList(), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"), /* selectCols */
      "v1 = NULL", /* predicate1 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList("v2"), /* coveringCols1 */
      true,  /* addExtraIndex */
      "v1 = NULL and h1 = 1", /* predicate2 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList(), /* coveringCols2 */
      true, /* strongConsistency */
      String.format("Index Scan using %s.idx2", DEFAULT_TEST_KEYSPACE), /* expectedAccessMethod */
      "", /* expectedKeyConditions */
      "", /* expectedFilterConditions */
      Arrays.asList( /* rowsToInsert */
        "1, 1, 1, 1, NULL, 1", // Idx2 pred=true row and where clause satisfied
        "2, 1, 1, 1, NULL, 1" // Idx2 pred=false row
      )
    );

    // 1. Partial Index 1: WHERE clause => Idx predicate && non-full scan
    // 2. Partial Index 2: WHERE clause => Idx predicate && non-full scan
    //     Idx2 has longer predicate.
    // 3. Main table: Full scan
    // [Idx2 is chosen]
    testPartialIndexSelectInternal(
      "r1 = 1 and v1 = NULL", /* whereClause */
      "r1 = 1 and v1 = NULL", /* whereClauseWithoutIndexes */
      Arrays.asList(), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"), /* selectCols */
      "v1 = NULL", /* predicate1 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList("v2"), /* coveringCols1 */
      true,  /* addExtraIndex */
      "v1 = NULL and r1 = 1", /* predicate2 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList(), /* coveringCols2 */
      true, /* strongConsistency */
      String.format("Index Scan using %s.idx2", DEFAULT_TEST_KEYSPACE), /* expectedAccessMethod */
      "Key Conditions: (v1 = NULL) AND (r1 = 1)", /* expectedKeyConditions */
      "", /* expectedFilterConditions */
      Arrays.asList( /* rowsToInsert */
        "1, 1, 1, 1, NULL, 1", // Idx2 pred=true row and where clause satisfied
        "1, 1, 1, 2, NULL, 1" // Idx2 pred=false row
      )
    );

    // 1. Partial Index 1: WHERE clause => Idx predicate && full scan
    // 2. Partial Index 2: WHERE clause => Idx predicate && non-full scan
    //     Idx1 has longer predicate.
    // 3. Main table: Full scan
    // [Idx2 is chosen]
    testPartialIndexSelectInternal(
      "r2 = 2 and v1 = NULL", /* whereClause */
      "r2 = 2 and v1 = NULL", /* whereClauseWithoutIndexes */
      Arrays.asList(), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"), /* selectCols */
      "r2 = 2 and v1 = NULL", /* predicate1 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList("v2"), /* coveringCols1 */
      true,  /* addExtraIndex */
      "v1 = NULL", /* predicate2 */
      Arrays.asList("v1", "r2"), /* indexedHashCols1 */
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList(), /* coveringCols2 */
      true, /* strongConsistency */
      String.format("Index Scan using %s.idx2", DEFAULT_TEST_KEYSPACE), /* expectedAccessMethod */
      "Key Conditions: (v1 = NULL) AND (r2 = 2)", /* expectedKeyConditions */
      "", /* expectedFilterConditions */
      Arrays.asList( /* rowsToInsert */
        "1, 1, 1, 2, NULL, 1", // Idx2 pred=true row and where clause satisfied
        "2, 1, 1, 1, NULL, 1", // Idx2 pred=true row but where clause not satisfied
        "3, 1, 1, 1, 1, 1" // Idx2 pred=false row
      )
    );
  }

  /*
   * Part 1 -
   * --------
   * Execute tests (listed below) for different predicates on indexed cols -
   *  1. int col = NULL
   *  2. int col != NULL
   *  3. int col > 5
   *  4. bool col != false
   *  5. multi-column v1 = NULL and v2 = NULL,
   *  6. text col = 'dummy'
   *
   * Part 2 -
   * --------
   * Execute test list for -
   *  1. Predicate involves some of indexed cols and none of covered cols. Already covered above.
   *  2. Predicate involves none of indexed cols and some of covered cols.
   *  3. Predicate involves some of indexed cols and some of covered cols.
   *  4. Predicate involves none of indexed cols and none of indexed cols.
   *
   * List of tests executed for each case are -
   *    1. Write path (using only INSERT/UPDATE) - testPartialIndexWritesInternal()
   *    2. Simple SELECT query path - testPartialIndexSelectInternal().
   *        This is for testing a basic selection policy of partial index for each combination.
   *        This is separate from the tests in testPartialIndexSelectionPolicy which tests all
   *        selectivity policies using a single table and some predicates.
   *    3. Delete path (using DELETE/ all null UPDATE statement) - testPartialIndexDeletesInternal()
   */
  public void testPartialIndexPart1_1(boolean strongConsistency, boolean isUnique)
      throws Exception {
    // Predicate: regular INT column v1=NULL | Indexed cols: [v1] | Covering cols: []
    createTable(
      String.format("create table %s " +
        "(h1 int, h2 int, r1 int, r2 int, v1 int, v2 int, " +
        "primary key ((h1, h2), r1, r2))", testTableName),
      strongConsistency);

    pkColCnt = 4;
    colCnt = 6;
    colNames = Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"); // pk cols first

    this.same_pk_i_c_both_pred_true_false_rows = false;
    this.same_pk_c_diff_i_both_pred_true_false_rows = true;
    this.same_pk_i_diff_c_both_pred_true_false_rows = false;
    this.same_pk_i_c_multiple_pred_false_rows = true;
    this.same_pk_c_diff_i_multiple_pred_false_rows = true;
    this.same_pk_i_diff_c_multiple_pred_false_rows = false;
    this.same_pk_i_c_multiple_pred_true_rows = true;
    this.same_pk_c_diff_i_multiple_pred_true_rows = false;
    this.same_pk_i_diff_c_multiple_pred_true_rows = false;

    // Flags for unique partial indexes.
    this.same_i_diff_pk_multiple_pred_true_rows = true;
    this.same_i_diff_pk_both_pred_true_false_rows = false;

    this.predTrueRows = Arrays.asList(
      Arrays.asList("1", "1", "1", "1", "NULL", "5"),
      Arrays.asList("1", "1", "1", "1", "NULL", "6"),
      Arrays.asList("1", "1", "1", "2", "NULL", "6")
    );
    this.predFalseRows = Arrays.asList(
      Arrays.asList("1", "1", "1", "1", "10", "7"),
      Arrays.asList("1", "1", "1", "1", "10", "8"),
      Arrays.asList("1", "1", "1", "1", "11", "8"),
      Arrays.asList("1", "1", "1", "2", "10", "8")
    );
    this.alreadyInsertedTrueRows = new ArrayList<Integer>();
    this.alreadyInsertedFalseRows = new ArrayList<Integer>();

    testPartialIndexWritesInternal(
      "v1 = NULL", /* predicate */
      Arrays.asList("v1"), /* indexedCols */
      Arrays.asList(), /* coveringCols */
      strongConsistency,
      isUnique);

    // 1. Main table scan: full scan
    // 2. Partial Index: WHERE clause => Idx predicate && full scan
    // [Partial Index is chosen]
    testPartialIndexSelectInternal(
      "h1 = 1 and v1 = NULL", /* whereClause */
      "h1 = 1 and v1 = NULL", /* whereClauseWithoutIndexes */
      Arrays.asList(), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"), /* selectCols */
      "v1 = NULL", /* predicate1 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList(), /* coveringCols1 */
      false,  /* addExtraIndex */
      "", /* predicate2 */
      Arrays.asList(), /* indexedHashCols2 */
      Arrays.asList(), /* indexedRangeCols2 */
      Arrays.asList(), /* coveringCols2 */
      strongConsistency, /* strongConsistency */
      String.format("Index Scan using %s.idx", DEFAULT_TEST_KEYSPACE), /* expectedAccessMethod */
      "", /* expectedKeyConditions */
      "Filter: (h1 = 1)", /* expectedFilterConditions */
      Arrays.asList( /* rowsToInsert */
        "1, 1, 1, 1, NULL, 1", // pred=true row and where clause satisfied
        "2, 1, 1, 1, NULL, 1", // pred=true row but where clause not satisfied
        "2, 2, 1, 1, 1, 1" // pred=false
      )
    );

    testPartialIndexDeletesInternal(
      "v1 = NULL", /* predicate */
      Arrays.asList("v1"), /* indexedCols */
      Arrays.asList(), /* coveringCols */
      strongConsistency,
      isUnique);

    session.execute(String.format("drop table %s", testTableName));
  }

  public void testPartialIndexPart1_2(boolean strongConsistency, boolean isUnique)
      throws Exception {
    // Predicate: regular INT column v1!=NULL | Indexed cols: [v1] | Covering cols: []
    createTable(
      String.format("create table %s " +
        "(h1 int, h2 int, r1 int, r2 int, v1 int, v2 int, " +
        "primary key ((h1, h2), r1, r2))", testTableName),
      strongConsistency);

    pkColCnt = 4;
    colCnt = 6;
    colNames = Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"); // pk cols first

    this.same_pk_i_c_both_pred_true_false_rows = false;
    this.same_pk_c_diff_i_both_pred_true_false_rows = true;
    this.same_pk_i_diff_c_both_pred_true_false_rows = false;
    this.same_pk_i_c_multiple_pred_false_rows = true;
    this.same_pk_c_diff_i_multiple_pred_false_rows = false;
    this.same_pk_i_diff_c_multiple_pred_false_rows = false;
    this.same_pk_i_c_multiple_pred_true_rows = true;
    this.same_pk_c_diff_i_multiple_pred_true_rows = true;
    this.same_pk_i_diff_c_multiple_pred_true_rows = false;

    // Flags for unique partial indexes.
    this.same_i_diff_pk_multiple_pred_true_rows = true;
    this.same_i_diff_pk_both_pred_true_false_rows = false;

    this.predTrueRows = Arrays.asList(
      Arrays.asList("1", "1", "1", "1", "1", "1"),
      Arrays.asList("1", "1", "1", "1", "1", "2"),
      Arrays.asList("1", "1", "1", "1", "2", "3"),
      Arrays.asList("1", "1", "1", "2", "1", "4")
    );
    this.predFalseRows = Arrays.asList(
      Arrays.asList("1", "1", "1", "1", "NULL", "1"),
      Arrays.asList("1", "1", "1", "1", "NULL", "2"),
      Arrays.asList("1", "1", "1", "2", "NULL", "3")
    );
    this.alreadyInsertedTrueRows = new ArrayList<Integer>();
    this.alreadyInsertedFalseRows = new ArrayList<Integer>();

    testPartialIndexWritesInternal(
      "v1 != NULL", /* predicate */
      Arrays.asList("v1"), /* indexedCols */
      Arrays.asList(), /* coveringCols */
      strongConsistency,
      isUnique);

    // 1. Main table scan: full scan
    // 2. Partial Index: WHERE clause => Idx predicate && full scan
    // [Partial Index is chosen]
    testPartialIndexSelectInternal(
      "h1 = 1 and v1 != NULL", /* whereClause */
      "h1 = 1 and v1 != NULL", /* whereClauseWithoutIndexes */
      Arrays.asList(), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"), /* selectCols */
      "v1 != NULL", /* predicate1 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */ // r1 added to make it full scan
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList(), /* coveringCols1 */
      false,  /* addExtraIndex */
      "", /* predicate2 */
      Arrays.asList(), /* indexedHashCols2 */
      Arrays.asList(), /* indexedRangeCols2 */
      Arrays.asList(), /* coveringCols2 */
      strongConsistency, /* strongConsistency */
      String.format("Index Scan using %s.idx", DEFAULT_TEST_KEYSPACE), /* expectedAccessMethod */
      "", /* expectedKeyConditions */
      "Filter: (h1 = 1)", /* expectedFilterConditions */
      Arrays.asList( /* rowsToInsert */
        "1, 1, 1, 1, 1, 1", // pred=true row and where clause satisfied
        "2, 1, 1, 1, 1, 1", // pred=true row but where clause not satisfied
        "2, 2, 1, 1, NULL, 1" // pred=false
      )
    );

    testPartialIndexDeletesInternal(
      "v1 != NULL", /* predicate */
      Arrays.asList("v1"), /* indexedCols */
      Arrays.asList(), /* coveringCols */
      strongConsistency,
      isUnique);

    session.execute(String.format("drop table %s", testTableName));
  }

  public void testPartialIndexPart1_3(boolean strongConsistency, boolean isUnique)
      throws Exception {
    // Predicate: regular INT column v1>5 | Indexed cols: [v1] | Covering cols: []
    createTable(
      String.format("create table %s " +
        "(h1 int, h2 int, r1 int, r2 int, v1 int, v2 int, " +
        "primary key ((h1, h2), r1, r2))", testTableName),
      strongConsistency);

    pkColCnt = 4;
    colCnt = 6;
    colNames = Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"); // pk cols first

    this.same_pk_i_c_both_pred_true_false_rows = false;
    this.same_pk_c_diff_i_both_pred_true_false_rows = true;
    this.same_pk_i_diff_c_both_pred_true_false_rows = false;
    this.same_pk_i_c_multiple_pred_false_rows = true;
    this.same_pk_c_diff_i_multiple_pred_false_rows = true;
    this.same_pk_i_diff_c_multiple_pred_false_rows = false;
    this.same_pk_i_c_multiple_pred_true_rows = true;
    this.same_pk_c_diff_i_multiple_pred_true_rows = true;
    this.same_pk_i_diff_c_multiple_pred_true_rows = false;

    // Flags for unique partial indexes.
    this.same_i_diff_pk_multiple_pred_true_rows = true;
    this.same_i_diff_pk_both_pred_true_false_rows = false;

    this.predTrueRows = Arrays.asList(
      Arrays.asList("1", "1", "1", "1", "6", "1"),
      Arrays.asList("1", "1", "1", "1", "6", "2"),
      Arrays.asList("1", "1", "1", "1", "7", "3"),
      Arrays.asList("1", "1", "1", "2", "6", "4")
    );
    this.predFalseRows = Arrays.asList(
      Arrays.asList("1", "1", "1", "1", "4", "1"),
      Arrays.asList("1", "1", "1", "1", "4", "2"),
      Arrays.asList("1", "1", "1", "1", "3", "3"),
      Arrays.asList("1", "1", "1", "2", "3", "3")
    );
    this.alreadyInsertedTrueRows = new ArrayList<Integer>();
    this.alreadyInsertedFalseRows = new ArrayList<Integer>();

    testPartialIndexWritesInternal(
      "v1 > 5", /* predicate */
      Arrays.asList("v1"), /* indexedCols */
      Arrays.asList(), /* coveringCols */
      strongConsistency,
      isUnique);

    // 1. Main table scan: full scan
    // 2. Partial Index: WHERE clause => Idx predicate && full scan
    // [Partial Index is chosen]
    testPartialIndexSelectInternal(
      "h1 = 1 and v1 > 5", /* whereClause */
      "h1 = 1 and v1 > 5", /* whereClauseWithoutIndexes */
      Arrays.asList(), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"), /* selectCols */
      "v1 > 5", /* predicate1 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */ // r1 added to make it full scan
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList(), /* coveringCols1 */
      false,  /* addExtraIndex */
      "", /* predicate2 */
      Arrays.asList(), /* indexedHashCols2 */
      Arrays.asList(), /* indexedRangeCols2 */
      Arrays.asList(), /* coveringCols2 */
      strongConsistency,
      String.format("Index Scan using %s.idx", DEFAULT_TEST_KEYSPACE), /* expectedAccessMethod */
      "", /* expectedKeyConditions */
      "Filter: (h1 = 1)", /* expectedFilterConditions */
      Arrays.asList( /* rowsToInsert */
        "1, 1, 1, 1, 6, 1", // pred=true row and where clause satisfied
        "2, 1, 1, 1, 7, 1", // pred=true row but where clause not satisfied
        "2, 2, 1, 1, 1, 1" // pred=false
      )
    );

    testPartialIndexDeletesInternal(
      "v1 > 5", /* predicate */
      Arrays.asList("v1"), /* indexedCols */
      Arrays.asList(), /* coveringCols */
      strongConsistency,
      isUnique);

    session.execute(String.format("drop table %s", testTableName));
  }

  public void testPartialIndexPart1_4(boolean strongConsistency, boolean isUnique)
      throws Exception {
    // Predicate: regular boolean column v1!=false | Indexed cols: [v1] | Covering cols: []
    createTable(
      String.format("create table %s " +
        "(h1 int, h2 int, r1 int, r2 int, v1 boolean, v2 int, " +
        "primary key ((h1, h2), r1, r2))", testTableName),
      strongConsistency);

    pkColCnt = 4;
    colCnt = 6;
    colNames = Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"); // pk cols first

    this.same_pk_i_c_both_pred_true_false_rows = false;
    this.same_pk_c_diff_i_both_pred_true_false_rows = true;
    this.same_pk_i_diff_c_both_pred_true_false_rows = false;
    this.same_pk_i_c_multiple_pred_false_rows = true;
    this.same_pk_c_diff_i_multiple_pred_false_rows = false;
    this.same_pk_i_diff_c_multiple_pred_false_rows = false;
    this.same_pk_i_c_multiple_pred_true_rows = true;
    this.same_pk_c_diff_i_multiple_pred_true_rows = true;
    this.same_pk_i_diff_c_multiple_pred_true_rows = false;

    // Flags for unique partial indexes.
    this.same_i_diff_pk_multiple_pred_true_rows = true;
    this.same_i_diff_pk_both_pred_true_false_rows = false;

    this.predTrueRows = Arrays.asList(
      Arrays.asList("1", "1", "1", "1", "true", "5"),
      Arrays.asList("1", "1", "1", "1", "true", "6"),
      Arrays.asList("1", "1", "1", "1", "NULL", "5"),
      Arrays.asList("1", "1", "1", "2", "true", "6"),
      Arrays.asList("1", "1", "1", "2", "NULL", "6")
    );
    this.predFalseRows = Arrays.asList(
      Arrays.asList("1", "1", "1", "1", "false", "7"),
      Arrays.asList("1", "1", "1", "1", "false", "8"),
      Arrays.asList("1", "1", "1", "2", "false", "9")
    );
    this.alreadyInsertedTrueRows = new ArrayList<Integer>();
    this.alreadyInsertedFalseRows = new ArrayList<Integer>();

    testPartialIndexWritesInternal(
      "v1 != false", /* predicate */
      Arrays.asList("v1"), /* indexedCols */
      Arrays.asList(), /* coveringCols */
      strongConsistency,
      isUnique);

    // 1. Main table scan: full scan
    // 2. Partial Index: WHERE clause => Idx predicate && full scan
    // [Partial Index is chosen]
    testPartialIndexSelectInternal(
      "h1 = 1 and v1 != false", /* whereClause */
      "h1 = 1 and v1 != false", /* whereClauseWithoutIndexes */
      Arrays.asList(), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"), /* selectCols */
      "v1 != false", /* predicate1 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */ // r1 added to make it full scan
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList(), /* coveringCols1 */
      false,  /* addExtraIndex */
      "", /* predicate2 */
      Arrays.asList(), /* indexedHashCols2 */
      Arrays.asList(), /* indexedRangeCols2 */
      Arrays.asList(), /* coveringCols2 */
      strongConsistency,
      String.format("Index Scan using %s.idx", DEFAULT_TEST_KEYSPACE), /* expectedAccessMethod */
      "", /* expectedKeyConditions */
      "Filter: (h1 = 1)", /* expectedFilterConditions */
      Arrays.asList( /* rowsToInsert */
        "1, 1, 1, 1, true, 5", // pred=true row and where clause satisfied
        "2, 1, 1, 1, NULL, 1", // pred=true row but where clause not satisfied
        "2, 2, 1, 1, false, 1" // pred=false
      )
    );

    testPartialIndexDeletesInternal(
      "v1 != false", /* predicate */
      Arrays.asList("v1"), /* indexedCols */
      Arrays.asList(), /* coveringCols */
      strongConsistency,
      isUnique);

    session.execute(String.format("drop table %s", testTableName));
  }

  public void testPartialIndexPart1_5(boolean strongConsistency, boolean isUnique)
      throws Exception {
    // Predicate: multi col =NULL case; v1=NULL and v2=NULL | Indexed cols: [v1, v2] |
    //    Covering cols: []
    createTable(
      String.format("create table %s " +
        "(h1 int, h2 int, r1 int, r2 int, v1 int, v2 int, v3 int, " +
        "primary key ((h1, h2), r1, r2))", testTableName),
      strongConsistency);

    pkColCnt = 4;
    colCnt = 7;
    colNames = Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2", "v3"); // pk cols first

    this.same_pk_i_c_both_pred_true_false_rows = false;
    this.same_pk_c_diff_i_both_pred_true_false_rows = true;
    this.same_pk_i_diff_c_both_pred_true_false_rows = false;
    this.same_pk_i_c_multiple_pred_false_rows = true;
    this.same_pk_c_diff_i_multiple_pred_false_rows = true;
    this.same_pk_i_diff_c_multiple_pred_false_rows = false;
    this.same_pk_i_c_multiple_pred_true_rows = true;
    this.same_pk_c_diff_i_multiple_pred_true_rows = false;
    this.same_pk_i_diff_c_multiple_pred_true_rows = false;

    // Flags for unique partial indexes.
    this.same_i_diff_pk_multiple_pred_true_rows = true;
    this.same_i_diff_pk_both_pred_true_false_rows = false;

    this.predTrueRows = Arrays.asList(
      Arrays.asList("1", "1", "1", "1", "NULL", "NULL", "5"),
      Arrays.asList("1", "1", "1", "1", "NULL", "NULL", "6"),
      Arrays.asList("1", "1", "1", "2", "NULL", "NULL", "5")
    );
    this.predFalseRows = Arrays.asList(
      Arrays.asList("1", "1", "1", "1", "NULL", "10", "5"),
      Arrays.asList("1", "1", "1", "1", "NULL", "10", "6"),
      Arrays.asList("1", "1", "1", "1", "10", "NULL", "5"),
      Arrays.asList("1", "1", "1", "2", "NULL", "10", "5")
    );

    this.alreadyInsertedTrueRows = new ArrayList<Integer>();
    this.alreadyInsertedFalseRows = new ArrayList<Integer>();

    testPartialIndexWritesInternal(
        "v1 = NULL and v2 = NULL", /* predicate */
        Arrays.asList("v1", "v2"), /* indexedCols */
        Arrays.asList(), /* coveringCols */
        strongConsistency,
        isUnique);

    // 1. Main table scan: full scan
    // 2. Partial Index: WHERE clause => Idx predicate && full scan
    // [Partial Index is chosen]
    testPartialIndexSelectInternal(
      "h1 = 1 and v1 = NULL and v2 = NULL", /* whereClause */
      "h1 = 1 and v1 = NULL and v2 = NULL", /* whereClauseWithoutIndexes */
      Arrays.asList(), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"), /* selectCols */
      "v1 = NULL and v2 = NULL", /* predicate1 */
      Arrays.asList("v1", "v2", "r1"), /* indexedHashCols1 */ // r1 added to make it full scan
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList(), /* coveringCols1 */
      false,  /* addExtraIndex */
      "", /* predicate2 */
      Arrays.asList(), /* indexedHashCols2 */
      Arrays.asList(), /* indexedRangeCols2 */
      Arrays.asList(), /* coveringCols2 */
      strongConsistency,
      String.format("Index Only Scan using %s.idx",
        DEFAULT_TEST_KEYSPACE), /* expectedAccessMethod */
      "", /* expectedKeyConditions */
      "Filter: (h1 = 1)", /* expectedFilterConditions */
      Arrays.asList( /* rowsToInsert */
        "1, 1, 1, 1, NULL, NULL, 5", // pred=true row and where clause satisfied
        "2, 1, 1, 1, NULL, NULL, 5", // pred=true row but where clause not satisfied
        "2, 2, 1, 1, 1, 1, 5" // pred=false
      )
    );

    testPartialIndexDeletesInternal(
      "v1 = NULL and v2 = NULL", /* predicate */
      Arrays.asList("v1", "v2"), /* indexedCols */
      Arrays.asList(), /* coveringCols */
      strongConsistency,
      isUnique);

    session.execute(String.format("drop table %s", testTableName));
  }

  public void testPartialIndexPart1_6(boolean strongConsistency, boolean isUnique)
      throws Exception {
    // Predicate: regular text column v1='dummy' | Indexed cols: [v1] | Covering cols: []
    createTable(
      String.format("create table %s " +
        "(h1 int, h2 int, r1 int, r2 int, v1 text, v2 int, " +
        "primary key ((h1, h2), r1, r2))", testTableName),
      strongConsistency);

    pkColCnt = 4;
    colCnt = 6;
    colNames = Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"); // pk cols first

    this.same_pk_i_c_both_pred_true_false_rows = false;
    this.same_pk_c_diff_i_both_pred_true_false_rows = true;
    this.same_pk_i_diff_c_both_pred_true_false_rows = false;
    this.same_pk_i_c_multiple_pred_false_rows = true;
    this.same_pk_c_diff_i_multiple_pred_false_rows = true;
    this.same_pk_i_diff_c_multiple_pred_false_rows = false;
    this.same_pk_i_c_multiple_pred_true_rows = true;
    this.same_pk_c_diff_i_multiple_pred_true_rows = false;
    this.same_pk_i_diff_c_multiple_pred_true_rows = false;

    // Flags for unique partial indexes.
    this.same_i_diff_pk_multiple_pred_true_rows = true;
    this.same_i_diff_pk_both_pred_true_false_rows = false;

    this.predTrueRows = Arrays.asList(
      Arrays.asList("1", "1", "1", "1", "\'dummy\'", "5"),
      Arrays.asList("1", "1", "1", "1", "\'dummy\'", "6"),
      Arrays.asList("1", "1", "1", "2", "\'dummy\'", "6")
    );
    this.predFalseRows = Arrays.asList(
      Arrays.asList("1", "1", "1", "1", "\'dummy_1\'", "7"),
      Arrays.asList("1", "1", "1", "1", "\'dummy_1\'", "8"),
      Arrays.asList("1", "1", "1", "1", "\'dummy_2\'", "8"),
      Arrays.asList("1", "1", "1", "2", "\'dummy_1\'", "8")
    );
    this.alreadyInsertedTrueRows = new ArrayList<Integer>();
    this.alreadyInsertedFalseRows = new ArrayList<Integer>();

    testPartialIndexWritesInternal(
      "v1 = \'dummy\'", /* predicate */
      Arrays.asList("v1"), /* indexedCols */
      Arrays.asList(), /* coveringCols */
      strongConsistency,
      isUnique);

    // 1. Main table scan: full scan
    // 2. Partial Index: WHERE clause => Idx predicate && full scan
    // [Partial Index is chosen]
    testPartialIndexSelectInternal(
      "h1 = 1 and v1 = 'dummy'", /* whereClause */
      "h1 = 1 and v1 = 'dummy'", /* whereClauseWithoutIndexes */
      Arrays.asList(), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"), /* selectCols */
      "v1 = 'dummy'", /* predicate1 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */ // r1 added to make it full scan
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList(), /* coveringCols1 */
      false,  /* addExtraIndex */
      "", /* predicate2 */
      Arrays.asList(), /* indexedHashCols2 */
      Arrays.asList(), /* indexedRangeCols2 */
      Arrays.asList(), /* coveringCols2 */
      strongConsistency, /* strongConsistency */
      String.format("Index Scan using %s.idx", DEFAULT_TEST_KEYSPACE), /* expectedAccessMethod */
      "", /* expectedKeyConditions */
      "Filter: (h1 = 1)", /* expectedFilterConditions */
      Arrays.asList( /* rowsToInsert */
        "1, 1, 1, 1, 'dummy', 1", // pred=true row and where clause satisfied
        "2, 1, 1, 1, 'dummy', 1", // pred=true row but where clause not satisfied
        "2, 2, 1, 1, 'dummy_1', 1" // pred=false
      )
    );

    testPartialIndexDeletesInternal(
      "v1 = 'dummy'", /* predicate */
      Arrays.asList("v1"), /* indexedCols */
      Arrays.asList(), /* coveringCols */
      strongConsistency,
      isUnique);

    session.execute(String.format("drop table %s", testTableName));
  }

  /*
    * Part 2 -
    * --------
    * 1. Predicate involves some of indexed cols and none of covered cols. Already covered above.
    * 2. Predicate involves none of indexed cols and some of covered cols.
    * 3. Predicate involves some of indexed cols and some of covered cols.
    * 4. Predicate involves none of indexed cols and none of indexed cols.
    */
  public void testPartialIndexPart2_2(boolean strongConsistency, boolean isUnique)
      throws Exception {
    // Predicate: regular INT column v2>5 | Indexed cols: [v1] | Covering cols: [v2]
    createTable(
      String.format("create table %s " +
        "(h1 int, h2 int, r1 int, r2 int, v1 int, v2 int, v3 int, " +
        "primary key ((h1, h2), r1, r2))", testTableName),
      strongConsistency);

    pkColCnt = 4;
    colCnt = 7;
    colNames = Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2", "v3"); // pk cols first

    this.same_pk_i_c_both_pred_true_false_rows = false;
    this.same_pk_c_diff_i_both_pred_true_false_rows = false;
    this.same_pk_i_diff_c_both_pred_true_false_rows = true;
    this.same_pk_i_c_multiple_pred_false_rows = true;
    this.same_pk_c_diff_i_multiple_pred_false_rows = true;
    this.same_pk_i_diff_c_multiple_pred_false_rows = true;
    this.same_pk_i_c_multiple_pred_true_rows = true;
    this.same_pk_c_diff_i_multiple_pred_true_rows = true;
    this.same_pk_i_diff_c_multiple_pred_true_rows = true;

    // Flags for unique partial indexes.
    this.same_i_diff_pk_multiple_pred_true_rows = true;
    this.same_i_diff_pk_both_pred_true_false_rows = true;

    this.predTrueRows = Arrays.asList(
      Arrays.asList("1", "1", "1", "1", "1", "6", "1"),
      Arrays.asList("1", "1", "1", "1", "1", "6", "2"),
      Arrays.asList("1", "1", "1", "1", "2", "6", "3"),
      Arrays.asList("1", "1", "1", "1", "1", "7", "4"),
      Arrays.asList("1", "1", "1", "2", "1", "6", "5")
    );
    this.predFalseRows = Arrays.asList(
      Arrays.asList("1", "1", "1", "1", "1", "4", "1"),
      Arrays.asList("1", "1", "1", "1", "1", "4", "2"),
      Arrays.asList("1", "1", "1", "1", "2", "4", "3"),
      Arrays.asList("1", "1", "1", "1", "1", "3", "4"),
      Arrays.asList("1", "1", "1", "2", "1", "3", "5")
    );
    this.alreadyInsertedTrueRows = new ArrayList<Integer>();
    this.alreadyInsertedFalseRows = new ArrayList<Integer>();

    testPartialIndexWritesInternal(
      "v2 > 5", /* predicate */
      Arrays.asList("v1"), /* indexedCols */
      Arrays.asList("v2"), /* coveringCols */
      strongConsistency,
      isUnique);

    // 1. Main table scan: full scan
    // 2. Partial Index: WHERE clause => Idx predicate && full scan
    // [Partial Index is chosen]
    testPartialIndexSelectInternal(
      "h1 = 1 and v2 > 5", /* whereClause */
      "h1 = 1 and v2 > 5", /* whereClauseWithoutIndexes */
      Arrays.asList(), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2", "v3"), /* selectCols */
      "v2 > 5", /* predicate1 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */ // r1 added to make it full scan
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList("v2"), /* coveringCols1 */
      false,  /* addExtraIndex */
      "", /* predicate2 */
      Arrays.asList(), /* indexedHashCols2 */
      Arrays.asList(), /* indexedRangeCols2 */
      Arrays.asList(), /* coveringCols2 */
      strongConsistency,
      String.format("Index Scan using %s.idx", DEFAULT_TEST_KEYSPACE), /* expectedAccessMethod */
      "", /* expectedKeyConditions */
      "Filter: (h1 = 1)", /* expectedFilterConditions */
      Arrays.asList( /* rowsToInsert */
        "1, 1, 1, 1, 1, 6, 1", // pred=true row and where clause satisfied
        "2, 1, 1, 1, 1, 7, 2", // pred=true row but where clause not satisfied
        "2, 2, 1, 1, 1, 1, 3" // pred=false
      )
    );

    testPartialIndexDeletesInternal(
      "v2 > 5", /* predicate */
      Arrays.asList("v1"), /* indexedCols */
      Arrays.asList("v2"), /* coveringCols */
      strongConsistency,
      isUnique);

    session.execute(String.format("drop table %s", testTableName));
  }

  public void testPartialIndexPart2_3(boolean strongConsistency, boolean isUnique)
      throws Exception {
    // Predicate: regular INT column v1>5 and v2>5 | Indexed cols: [v1] | Covering cols: [v2]
    createTable(
      String.format("create table %s " +
        "(h1 int, h2 int, r1 int, r2 int, v1 int, v2 int, v3 int, " +
        "primary key ((h1, h2), r1, r2))", testTableName),
      strongConsistency);

    pkColCnt = 4;
    colCnt = 7;
    colNames = Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2", "v3"); // pk cols first

    this.same_pk_i_c_both_pred_true_false_rows = false;
    this.same_pk_c_diff_i_both_pred_true_false_rows = true;
    this.same_pk_i_diff_c_both_pred_true_false_rows = true;
    this.same_pk_i_c_multiple_pred_false_rows = true;
    this.same_pk_c_diff_i_multiple_pred_false_rows = true;
    this.same_pk_i_diff_c_multiple_pred_false_rows = true;
    this.same_pk_i_c_multiple_pred_true_rows = true;
    this.same_pk_c_diff_i_multiple_pred_true_rows = true;
    this.same_pk_i_diff_c_multiple_pred_true_rows = true;

    // Flags for unique partial indexes.
    this.same_i_diff_pk_multiple_pred_true_rows = true;
    this.same_i_diff_pk_both_pred_true_false_rows = true;

    this.predTrueRows = Arrays.asList(
      Arrays.asList("1", "1", "1", "1", "6", "6", "1"),
      Arrays.asList("1", "1", "1", "1", "6", "6", "2"),
      Arrays.asList("1", "1", "1", "1", "7", "6", "3"),
      Arrays.asList("1", "1", "1", "1", "6", "7", "4"),
      Arrays.asList("1", "1", "1", "2", "6", "6", "1")
    );
    this.predFalseRows = Arrays.asList(
      Arrays.asList("1", "1", "1", "1", "6", "4", "1"),
      Arrays.asList("1", "1", "1", "1", "4", "6", "2"),
      Arrays.asList("1", "1", "1", "1", "4", "6", "3"),
      Arrays.asList("1", "1", "1", "1", "3", "6", "4"),
      Arrays.asList("1", "1", "1", "1", "4", "7", "5"),
      Arrays.asList("1", "1", "1", "2", "6", "4", "6")
    );
    this.alreadyInsertedTrueRows = new ArrayList<Integer>();
    this.alreadyInsertedFalseRows = new ArrayList<Integer>();

    testPartialIndexWritesInternal(
      "v1 > 5 and v2 > 5", /* predicate */
      Arrays.asList("v1"), /* indexedCols */
      Arrays.asList("v2"), /* coveringCols */
      strongConsistency,
      isUnique);

    // 1. Main table scan: full scan
    // 2. Partial Index: WHERE clause => Idx predicate && full scan
    // [Partial Index is chosen]
    testPartialIndexSelectInternal(
      "h1 = 1 and v1 > 5 and v2 > 5", /* whereClause */
      "h1 = 1 and v1 > 5 and v2 > 5", /* whereClauseWithoutIndexes */
      Arrays.asList(), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2", "v3"), /* selectCols */
      "v1 > 5 and v2 > 5", /* predicate1 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */ // r1 added to make it full scan
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList("v2"), /* coveringCols1 */
      false,  /* addExtraIndex */
      "", /* predicate2 */
      Arrays.asList(), /* indexedHashCols2 */
      Arrays.asList(), /* indexedRangeCols2 */
      Arrays.asList(), /* coveringCols2 */
      strongConsistency,
      String.format("Index Scan using %s.idx", DEFAULT_TEST_KEYSPACE), /* expectedAccessMethod */
      "", /* expectedKeyConditions */
      "Filter: (h1 = 1)", /* expectedFilterConditions */
      Arrays.asList( /* rowsToInsert */
        "1, 1, 1, 1, 6, 6, 1", // pred=true row and where clause satisfied
        "2, 1, 1, 1, 7, 8, 2", // pred=true row but where clause not satisfied
        "2, 2, 1, 1, 8, 4, 3" // pred=false
      )
    );

    testPartialIndexDeletesInternal(
      "v1 > 5 and v2 > 5", /* predicate */
      Arrays.asList("v1"), /* indexedCols */
      Arrays.asList("v2"), /* coveringCols */
      strongConsistency,
      isUnique);

    session.execute(String.format("drop table %s", testTableName));
  }

  public void testPartialIndexPart2_4(boolean strongConsistency, boolean isUnique)
      throws Exception {
    // Predicate: regular INT column v3>5 | Indexed cols: [v1] | Covering cols: [v2]
    createTable(
      String.format("create table %s " +
        "(h1 int, h2 int, r1 int, r2 int, v1 int, v2 int, v3 int, " +
        "primary key ((h1, h2), r1, r2))", testTableName),
      strongConsistency);

    pkColCnt = 4;
    colCnt = 7;
    colNames = Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2", "v3"); // pk cols first

    this.same_pk_i_c_both_pred_true_false_rows = true;
    this.same_pk_c_diff_i_both_pred_true_false_rows = true;
    this.same_pk_i_diff_c_both_pred_true_false_rows = true;
    this.same_pk_i_c_multiple_pred_false_rows = true;
    this.same_pk_c_diff_i_multiple_pred_false_rows = true;
    this.same_pk_i_diff_c_multiple_pred_false_rows = true;
    this.same_pk_i_c_multiple_pred_true_rows = true;
    this.same_pk_c_diff_i_multiple_pred_true_rows = true;
    this.same_pk_i_diff_c_multiple_pred_true_rows = true;

    // Flags for unique partial indexes.
    this.same_i_diff_pk_multiple_pred_true_rows = true;
    this.same_i_diff_pk_both_pred_true_false_rows = true;

    this.predTrueRows = Arrays.asList(
      Arrays.asList("1", "1", "1", "1", "1", "1", "6"),
      Arrays.asList("1", "1", "1", "1", "1", "1", "7"),
      Arrays.asList("1", "1", "1", "1", "2", "1", "6"),
      Arrays.asList("1", "1", "1", "1", "1", "2", "6"),
      Arrays.asList("1", "1", "1", "2", "1", "1", "6")
    );
    this.predFalseRows = Arrays.asList(
      Arrays.asList("1", "1", "1", "1", "1", "1", "4"),
      Arrays.asList("1", "1", "1", "1", "2", "1", "4"),
      Arrays.asList("1", "1", "1", "1", "1", "2", "4"),
      Arrays.asList("1", "1", "1", "1", "1", "1", "5"),
      Arrays.asList("1", "1", "1", "2", "1", "1", "4")
    );
    this.alreadyInsertedTrueRows = new ArrayList<Integer>();
    this.alreadyInsertedFalseRows = new ArrayList<Integer>();

    testPartialIndexWritesInternal(
      "v3 > 5", /* predicate */
      Arrays.asList("v1"), /* indexedCols */
      Arrays.asList("v2"), /* coveringCols */
      strongConsistency,
      isUnique);

    // 1. Main table scan: full scan
    // 2. Partial Index: WHERE clause => Idx predicate && full scan
    // [Partial Index is chosen]
    testPartialIndexSelectInternal(
      "h1 = 1 and v3 > 5", /* whereClause */
      "h1 = 1 and v3 > 5", /* whereClauseWithoutIndexes */
      Arrays.asList(), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2", "v3"), /* selectCols */
      "v3 > 5", /* predicate1 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */ // r1 added to make it full scan
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList("v2"), /* coveringCols1 */
      false,  /* addExtraIndex */
      "", /* predicate2 */
      Arrays.asList(), /* indexedHashCols2 */
      Arrays.asList(), /* indexedRangeCols2 */
      Arrays.asList(), /* coveringCols2 */
      strongConsistency,
      String.format("Index Scan using %s.idx", DEFAULT_TEST_KEYSPACE), /* expectedAccessMethod */
      "", /* expectedKeyConditions */
      "Filter: (h1 = 1)", /* expectedFilterConditions */
      Arrays.asList( /* rowsToInsert */
        "1, 1, 1, 1, 1, 1, 6", // pred=true row and where clause satisfied
        "2, 1, 1, 1, 1, 1, 7", // pred=true row but where clause not satisfied
        "2, 2, 1, 1, 1, 1, 4" // pred=false
      )
    );

    // Misc test where predicate involves none of indexed cols and none of indexed cols
    // but still selectCols are such that the Index scan is covered i.e., an Index Only Scan.
    testPartialIndexSelectInternal(
      "h1 = 1 and v3 > 5", /* whereClause */
      "h1 = 1 and v3 > 5", /* whereClauseWithoutIndexes */
      Arrays.asList(), /* bindValues */
      Arrays.asList("h1", "h2", "r1", "r2", "v1", "v2"), /* selectCols */
      "v3 > 5", /* predicate1 */
      Arrays.asList("v1", "r1"), /* indexedHashCols1 */ // r1 added to make it full scan
      Arrays.asList(), /* indexedRangeCols1 */
      Arrays.asList("v2"), /* coveringCols1 */
      false,  /* addExtraIndex */
      "", /* predicate2 */
      Arrays.asList(), /* indexedHashCols2 */
      Arrays.asList(), /* indexedRangeCols2 */
      Arrays.asList(), /* coveringCols2 */
      strongConsistency,
      String.format("Index Only Scan using %s.idx", DEFAULT_TEST_KEYSPACE),
        /* expectedAccessMethod */
      "", /* expectedKeyConditions */
      "Filter: (h1 = 1)", /* expectedFilterConditions */
      Arrays.asList( /* rowsToInsert */
        "1, 1, 1, 1, 1, 1, 6", // pred=true row and where clause satisfied
        "2, 1, 1, 1, 1, 1, 7", // pred=true row but where clause not satisfied
        "2, 2, 1, 1, 1, 1, 4" // pred=false
      )
    );

    testPartialIndexDeletesInternal(
      "v3 > 5", /* predicate */
      Arrays.asList("v1"), /* indexedCols */
      Arrays.asList("v2"), /* coveringCols */
      strongConsistency,
      isUnique);

    session.execute(String.format("drop table %s", testTableName));
  }

  @Test
  public void testPartialIndexPart1_1() throws Exception {
    testPartialIndexPart1_1(true /* strongConsistency */, false /* isUnique */);
    testPartialIndexPart1_1(false /* strongConsistency */, false /* isUnique */);
  }

  @Test
  public void testUniquePartialIndexPart1_1() throws Exception {
    testPartialIndexPart1_1(true /* strongConsistency */, true /* isUnique */);
    testPartialIndexPart1_1(false /* strongConsistency */, true /* isUnique */);
  }

  @Test
  public void testPartialIndexPart1_2() throws Exception {
    testPartialIndexPart1_2(true /* strongConsistency */, false /* isUnique */);
    testPartialIndexPart1_2(false /* strongConsistency */, false /* isUnique */);
  }

  @Test
  public void testUniquePartialIndexPart1_2() throws Exception {
    testPartialIndexPart1_2(true /* strongConsistency */, true /* isUnique */);
    testPartialIndexPart1_2(false /* strongConsistency */, true /* isUnique */);
  }

  @Test
  public void testPartialIndexPart1_3() throws Exception {
    testPartialIndexPart1_3(true /* strongConsistency */, false /* isUnique */);
    testPartialIndexPart1_3(false /* strongConsistency */, false /* isUnique */);
  }

  @Test
  public void testUniquePartialIndexPart1_3() throws Exception {
    testPartialIndexPart1_3(true /* strongConsistency */, true /* isUnique */);
    testPartialIndexPart1_3(false /* strongConsistency */, true /* isUnique */);
  }

  @Test
  public void testPartialIndexPart1_4() throws Exception {
    testPartialIndexPart1_4(true /* strongConsistency */, false /* isUnique */);
    testPartialIndexPart1_4(false /* strongConsistency */, false /* isUnique */);
  }

  @Test
  public void testUniquePartialIndexPart1_4() throws Exception {
    testPartialIndexPart1_4(true /* strongConsistency */, true /* isUnique */);
    testPartialIndexPart1_4(false /* strongConsistency */, true /* isUnique */);
  }

  @Test
  public void testPartialIndexPart1_5() throws Exception {
    testPartialIndexPart1_5(true /* strongConsistency */, false /* isUnique */);
    testPartialIndexPart1_5(false /* strongConsistency */, false /* isUnique */);
  }

  @Test
  public void testUniquePartialIndexPart1_5() throws Exception {
    testPartialIndexPart1_5(true /* strongConsistency */, true /* isUnique */);
    testPartialIndexPart1_5(false /* strongConsistency */, true /* isUnique */);
  }

  @Test
  public void testPartialIndexPart1_6() throws Exception {
    testPartialIndexPart1_6(true /* strongConsistency */, false /* isUnique */);
    testPartialIndexPart1_6(false /* strongConsistency */, false /* isUnique */);
  }

  @Test
  public void testUniquePartialIndexPart1_6() throws Exception {
    testPartialIndexPart1_6(true /* strongConsistency */, true /* isUnique */);
    testPartialIndexPart1_6(false /* strongConsistency */, true /* isUnique */);
  }

  @Test
  public void testPartialIndexPart2_2() throws Exception {
    testPartialIndexPart2_2(true /* strongConsistency */, false /* isUnique */);
    testPartialIndexPart2_2(false /* strongConsistency */, false /* isUnique */);
  }

  @Test
  public void testUniquePartialIndexPart2_2() throws Exception {
    testPartialIndexPart2_2(true /* strongConsistency */, true /* isUnique */);
    testPartialIndexPart2_2(false /* strongConsistency */, true /* isUnique */);
  }

  @Test
  public void testPartialIndexPart2_3() throws Exception {
    testPartialIndexPart2_3(true /* strongConsistency */, false /* isUnique */);
    testPartialIndexPart2_3(false /* strongConsistency */, false /* isUnique */);
  }

  @Test
  public void testUniquePartialIndexPart2_3() throws Exception {
    testPartialIndexPart2_3(true /* strongConsistency */, true /* isUnique */);
    testPartialIndexPart2_3(false /* strongConsistency */, true /* isUnique */);
  }

  @Test
  public void testPartialIndexPart2_4() throws Exception {
    testPartialIndexPart2_4(true /* strongConsistency */, false /* isUnique */);
    testPartialIndexPart2_4(false /* strongConsistency */, false /* isUnique */);
  }

  @Test
  public void testUniquePartialIndexPart2_4() throws Exception {
    testPartialIndexPart2_4(true /* strongConsistency */, true /* isUnique */);
    testPartialIndexPart2_4(false /* strongConsistency */, true /* isUnique */);
  }

  @Test
  public void testGuardrails() throws Exception {
    // Predicates not allowed on json/collection cols with subscripted args.
    // E.g.:
    //    create index ... where json_col->>'title' = NULL;
    //    create index ... where map_col['title'] = NULL;
    createTable(
      String.format("create table %s " +
        "(h1 int primary key, json_col jsonb, map_col map<int, int>, list_col list<int>, v1 int)",
        testTableName),
      true /* strongConsistency */);

    runInvalidStmt("create index idx on " + testTableName + "(v1) where json_col->>'title' = NULL");
    runInvalidStmt("create index idx on " + testTableName + "(v1) where map_col[1] = 1");
    runInvalidStmt("create index idx on " + testTableName + "(v1) where list_col[1] = 1");
    waitForReadPermsOnAllIndexes(testTableName);
    session.execute("drop table " + testTableName);
  }

  private static enum RenameIndexedColAllowed { ON, OFF };

  private void testAlterCol(RenameIndexedColAllowed renameIndexedColAllowed) throws Exception {
    createTable(String.format("create table %s (h1 int primary key, v1 int, v2 int)",
                              testTableName),
                true /* strongConsistency */);

    // Add, rename, drop column without presence of any secondary index.
    session.execute(String.format("alter table %s add v3 int", testTableName));
    session.execute(String.format("alter table %s rename v3 to v33", testTableName));
    session.execute(String.format("alter table %s rename v33 to v3", testTableName));
    session.execute(String.format("alter table %s drop v3", testTableName));

    // Add, rename, drop column with presence of a partial index. Check for these cases -
    //   1. Indexed col - Drop (fails), rename
    //   2. Col in predicate - Drop (fails), rename
    //   3. New col - Add, rename, drop
    createIndex(String.format("CREATE INDEX idx ON %s(v1) WHERE v2 != NULL", testTableName),
                true /* strongConsistency */);
    waitForReadPermsOnAllIndexes(testTableName);

    runInvalidStmt(String.format("alter table %s drop v1", testTableName),
                   "Can't drop column used in an index. Remove 'idx' index first and try again");
    boolean renameAllowed = renameIndexedColAllowed == RenameIndexedColAllowed.ON;
    if (renameAllowed) {
      session.execute(String.format("alter table %s rename v1 to v11", testTableName));
      session.execute(String.format("alter table %s rename v11 to v1", testTableName));
    } else {
      runInvalidStmt(String.format("alter table %s rename v1 to v11", testTableName),
          "Can't rename column used in an index. This column is used in 'idx' index");
      // The above command will fail, so column v11 will not exist.
      runInvalidStmt(String.format("alter table %s rename v11 to v1", testTableName),
          "Undefined Column. Column doesn't exist");
    }

    runInvalidStmt(String.format("alter table %s drop v2", testTableName),
                   "Can't drop column used in an index. Remove 'idx' index first and try again");
    if (renameAllowed) {
      session.execute(String.format("alter table %s rename v2 to v22", testTableName));
      session.execute(String.format("alter table %s rename v22 to v2", testTableName));
    } else {
      runInvalidStmt(String.format("alter table %s rename v2 to v22", testTableName),
          "Can't rename column used in an index. This column is used in 'idx' index");
      // The above command will fail, so column v22 will not exist.
      runInvalidStmt(String.format("alter table %s rename v22 to v2", testTableName),
          "Undefined Column. Column doesn't exist");
    }

    session.execute(String.format("alter table %s add v3 int", testTableName));
    session.execute(String.format("alter table %s rename v3 to v33", testTableName));
    session.execute(String.format("alter table %s rename v33 to v3", testTableName));
    session.execute(String.format("alter table %s drop v3", testTableName));

    // Add, rename, drop column with presence of a non-partial index. Check for these cases -
    //   1. Indexed col - Drop (fails), rename
    //   2. Non-indexed col - Rename, drop, re-add
    // #10364 - In case of a non-partial index, if we tried to drop a column that wasn't in the
    // index, it resulted in seg fault.
    session.execute("drop index idx");
    createIndex(String.format("CREATE INDEX idx ON %s(v1)", testTableName),
                true /* strongConsistency */);
    waitForReadPermsOnAllIndexes(testTableName);
    runInvalidStmt(String.format("alter table %s drop v1", testTableName),
                   "Can't drop column used in an index. Remove 'idx' index first and try again");
    if (renameAllowed) {
      session.execute(String.format("alter table %s rename v1 to v11", testTableName));
      session.execute(String.format("alter table %s rename v11 to v1", testTableName));
    } else {
      runInvalidStmt(String.format("alter table %s rename v1 to v11", testTableName),
          "Can't rename column used in an index. This column is used in 'idx' index");
      // The above command will fail, so column v11 will not exist.
      runInvalidStmt(String.format("alter table %s rename v11 to v1", testTableName),
          "Undefined Column. Column doesn't exist");
    }
    session.execute(String.format("alter table %s rename v2 to v22", testTableName));
    session.execute(String.format("alter table %s rename v22 to v2", testTableName));

    session.execute(String.format("alter table %s drop v2", testTableName));
    session.execute(String.format("alter table %s add v2 int", testTableName));
  }

  @Test
  public void testAlterCol() throws Exception {
    testAlterCol(RenameIndexedColAllowed.OFF);
  }

  @Test
  public void testAlterColRenameIndexedColAllowed() throws Exception {
    try {
      // By default: ycql_enable_alter_rename_column_with_index=false.
      restartClusterWithFlag("ycql_enable_alter_rename_column_with_index", "true");
      testAlterCol(RenameIndexedColAllowed.ON);
    } finally {
      destroyMiniCluster(); // Destroy the recreated cluster when done.
    }
  }

  @Test
  public void testMoreThanOneIndex() throws Exception {
    // For github issue #9345
    createTable(
      String.format("create table test (k int primary key, v int)"), true /* strongConsistency */);
    session.execute("create index idx on test(v) where v != NULL");
    session.execute("create index idx2 on test(v) where v = NULL");
    waitForReadPermsOnAllIndexes("test");
    session.execute("insert into test (k, v) values (1, 1)");
    assertQueryRowsUnordered("select * from test where v != NULL",
                             "Row[1, 1]");
    assertQueryRowsUnordered("select * from test where v = NULL");

    session.execute("update test set v=NULL where k=1");
    assertQueryRowsUnordered("select * from test where v != NULL");
    assertQueryRowsUnordered("select * from test where v = NULL",
                             "Row[1, NULL]");

    session.execute("update test set v=1 where k=1");
    assertQueryRowsUnordered("select * from test where v != NULL",
                             "Row[1, 1]");
    assertQueryRowsUnordered("select * from test where v = NULL");
  }

  @Test
  public void testPredicateInfoPresenceInSystemSchema() throws Exception {
    // When partial indexes support more complex expressions, extend this test.

    // Test an index_predicate with sub-expressions on columns of following data types -
    // TINYINT, SMALLINT, INT/INTEGER, BIGINT, VARINT, BOOLEAN and TEXT along with following
    // operators (when applicable) - =, !=, >, <, >=, <=
    createTable(
      String.format("create table test (h1 int, r1 int, " +
          "v1 tinyint, v2 smallint, v3 int, v4 bigint, v5 varint, v6 boolean, v7 text," +
          "primary key(h1, r1))"),
      true /* strongConsistency */);
    session.execute("create index idx on test (r1) where " +
        "v1 >= -128 and v1 <= 127 and v1 > -127 and v1 < 126 and " +
        "v2 >= -32768 and v2 <= 32767 and v2 > -32767 and v2 < 3276 and " +
        "v3 >= -2147483648 and v3 <= 2147483647 and v3 > -2147483647 and v3 < 2147483646 and " +
        "v4 >= -9223372036854775808 and v4 <= 9223372036854775807 and " +
            "v4 > -9223372036854775807 and v4 < 9223372036854775806 and " +
        "v5 >= -9999999999999999999 and v5 <= 9999999999999999999 and v5 > 1 and v5 < 2 and " +
        "v6 = true and v7 != 'hello';");
    waitForReadPermsOnAllIndexes("test");
    assertQueryRowsUnordered("select options from system_schema.indexes where index_name = 'idx'",
        "Row[{target=r1, h1, predicate=v1 >= -128 AND v1 <= 127 AND v1 > -127 AND v1 < 126 AND " +
          "v2 >= -32768 AND v2 <= 32767 AND v2 > -32767 AND v2 < 3276 AND " +
          "v3 >= -2147483648 AND v3 <= 2147483647 AND v3 > -2147483647 AND v3 < 2147483646 AND " +
          "v4 >= -9223372036854775808 AND v4 <= 9223372036854775807 AND " +
              "v4 > -9223372036854775807 AND v4 < 9223372036854775806 AND " +
          "v5 >= -9999999999999999999 AND v5 <= 9999999999999999999 AND v5 > 1 AND v5 < 2 AND " +
          "v6 = true AND v7 != 'hello'}]");
  }
}
