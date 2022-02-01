package org.yb.cdc.ysql;

import org.apache.log4j.Logger;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.cdc.CdcService;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.common.ExpectedRecord3Proto;
import org.yb.cdc.util.CDCSubscriber;
import org.yb.cdc.util.TestUtils;

import java.util.ArrayList;
import java.util.List;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.fail;

@RunWith(value = YBTestRunner.class)
public class TestProtoRecords extends CDCBaseClass {
  private final static Logger LOG = Logger.getLogger(TestProtoRecords.class);

  private void executeScriptAssertRecords(ExpectedRecord3Proto[] expectedRecords,
                                          String sqlScript) throws Exception {
    CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
    testSubscriber.createStream("proto");

    if (!sqlScript.isEmpty()) {
      TestUtils.runSqlScript(connection, sqlScript);
    } else {
      LOG.info("No SQL script specified...");
    }

    setServerFlag(getTserverHostAndPort(), CDC_INTENT_SIZE_GFLAG, "25");
    List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
    testSubscriber.getResponseFromCDC(outputList);

    int expRecordIndex = 0;
    int processedRecords = 0;
    for (int i = 0; i < outputList.size(); ++i) {
      // ignoring the DDLs
      if (outputList.get(i).getRowMessage().getOp() == CdcService.RowMessage.Op.DDL) {
        continue;
      }

      ExpectedRecord3Proto.checkRecord(outputList.get(i), expectedRecords[expRecordIndex++]);
      ++processedRecords;
    }
    // NOTE: processedRecords will be the same as expRecordIndex
    assertEquals(expectedRecords.length, processedRecords);
  }

  @Before
  public void setUp() throws Exception {
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
    statement.execute("create table test (a int primary key, b int, c int);");
  }

  @Test
  public void testMultipleOps() {
    try {
      ExpectedRecord3Proto[] expectedRecords = {
        new ExpectedRecord3Proto(1, 2, 3, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(1, 2, 4, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(1, 0, 0, CdcService.RowMessage.Op.DELETE),
        new ExpectedRecord3Proto(2, 2, 4, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.COMMIT),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(7, 8, 9, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(7, 17, 9, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.COMMIT),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(6, 7, 8, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(6, 7, 17, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(6, 0, 0, CdcService.RowMessage.Op.DELETE),
        new ExpectedRecord3Proto(15, 7, 17, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.COMMIT),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(11, 12, 13, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(11, 13, 13, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(11, 13, 14, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.COMMIT),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(12, 112, 113, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(12, 0, 0, CdcService.RowMessage.Op.DELETE),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.COMMIT),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(13, 113, 114, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(13, 113, 115, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(13, 0, 0, CdcService.RowMessage.Op.DELETE),
        new ExpectedRecord3Proto(14, 113, 115, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.COMMIT),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(17, 114, 115, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(17, 114, 116, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(17, 0, 0, CdcService.RowMessage.Op.DELETE),
        new ExpectedRecord3Proto(18, 114, 116, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(18, 115, 116, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(18, 115, 117, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.COMMIT),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(18, 116, 117, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(18, 116, 118, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(20, 21, 22, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(20, 22, 22, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(20, 22, 23, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(20, 0, 0, CdcService.RowMessage.Op.DELETE),
        new ExpectedRecord3Proto(21, 22, 23, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.COMMIT),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(21, 23, 23, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(21, 23, 24, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(21, 0, 0, CdcService.RowMessage.Op.DELETE),
        new ExpectedRecord3Proto(21, 23, 24, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.COMMIT),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(-1, -2, -3, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(-4, -5, -6, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(-11, -12, -13, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(-1, 0, 0, CdcService.RowMessage.Op.DELETE),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.COMMIT),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(404, 405, 406, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(104, 204, 304, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(0, 0, 0, CdcService.RowMessage.Op.COMMIT),
        new ExpectedRecord3Proto(41, 43, 44, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(41, 44, 45, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(41, 0, 0, CdcService.RowMessage.Op.DELETE),
        new ExpectedRecord3Proto(41, 43, 44, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(41, 44, 45, CdcService.RowMessage.Op.UPDATE)
      };

      executeScriptAssertRecords(expectedRecords, "cdc_long_script.sql");

    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour with proto records failed", e);
      fail();
    }
  }
}
