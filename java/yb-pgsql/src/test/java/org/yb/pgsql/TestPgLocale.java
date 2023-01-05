package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;

import com.google.common.collect.ImmutableMap;

import java.sql.SQLException;
import java.sql.Statement;
import java.util.Map;

import static org.yb.AssertionWrappers.assertGreaterThan;

@RunWith(YBTestRunnerNonTsanOnly.class)
public class TestPgLocale extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgLocale.class);

  // Translations included with Postgres (src/postgres/src/backend/po/*.po)
  private static final Map<String,String> LC_EXPECTED_MSGS = ImmutableMap.<String,String>builder()
    .put("de_DE.UTF-8", "FEHLER: Relation \u00bbnon_existing_relation\u00ab existiert nicht")
    .put("en_US.UTF-8", "ERROR: relation \"non_existing_relation\" does not exist")
    .put("es_ES.UTF-8", "ERROR: no existe la relaci\u00f3n \u00abnon_existing_relation\u00bb")
    .put("fr_FR.UTF-8", "ERREUR: la relation \u00ab non_existing_relation \u00bb n'existe pas")
    .put("it_IT.UTF-8", "ERRORE: la relazione \"non_existing_relation\" non esiste")
    .put("ja_JP.UTF-8",
         "ERROR: \u30ea\u30ec\u30fc\u30b7\u30e7\u30f3\"non_existing_relation\"" +
         "\u306f\u5b58\u5728\u3057\u307e\u305b\u3093")
    .put("ko_KR.UTF-8",
         "\uc624\ub958: \"non_existing_relation\" " +
         "\uc774\ub984\uc758 \ub9b4\ub808\uc774\uc158(relation)\uc774 \uc5c6\uc2b5\ub2c8\ub2e4")
    .put("pl_PL.UTF-8", "B\u0141\u0104D: relacja \"non_existing_relation\" nie istnieje")
    .put("ru_RU.UTF-8",
         "\u041e\u0428\u0418\u0411\u041a\u0410: " +
         "\u043e\u0442\u043d\u043e\u0448\u0435\u043d\u0438\u0435 \"non_existing_relation\" " +
         "\u043d\u0435 \u0441\u0443\u0449\u0435\u0441\u0442\u0432\u0443\u0435\u0442")
    .put("sv_SE.UTF-8", "FEL: relationen \"non_existing_relation\" existerar inte")
    .put("tr_TR.UTF-8", "HATA: \"non_existing_relation\" nesnesi mevcut de\u011fil")
    .put("zh_CN.UTF-8", "\u9519\u8bef: \u5173\u7cfb \"non_existing_relation\" \u4e0d\u5b58\u5728")
    .build();
  private static final String NO_LOCALES_MSG =
    "No supported locales found, please install one of: " + LC_EXPECTED_MSGS.keySet();

  @Test
  public void testNotExistingRelationMessage() throws Exception {
    int valid_locales = 0;
    try (Statement stmt = connection.createStatement()) {
      for (Map.Entry<String,String> entry : LC_EXPECTED_MSGS.entrySet()) {
        // The SET l_message command fails if system does not have the locale installed
        try {
          stmt.execute("SET lc_messages TO \"" + entry.getKey() + "\"");
        } catch (SQLException se) {
          LOG.info("Could not set locale " + entry.getKey(), se);
          continue;
        }
        valid_locales++;
        runInvalidQuery(stmt, "SELECT * FROM non_existing_relation", entry.getValue());
      }
    }
    // Make sure at least one traslation is correct
    assertGreaterThan(NO_LOCALES_MSG, valid_locales, 0);
    LOG.info("Successfully tested {} locales", valid_locales);
  }
}
