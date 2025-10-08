\set ON_ERROR_STOP on

REVOKE ALL ON TABLE test_table FROM admin;
DROP ROLE admin;

REVOKE ALL ON TABLE test_table FROM "CaseSensitiveRole";
DROP ROLE "CaseSensitiveRole";

REVOKE ALL ON TABLE test_table FROM "role_with_a space";
DROP ROLE "role_with_a space";

REVOKE ALL ON TABLE test_table FROM "Role with spaces";
DROP ROLE "Role with spaces";

REVOKE ALL ON TABLE test_table FROM "Role with a quote '";
DROP ROLE "Role with a quote '";

REVOKE ALL ON TABLE test_table FROM "Role with 'quotes'";
DROP ROLE "Role with 'quotes'";

REVOKE ALL ON TABLE test_table FROM "Role with a double quote """;
DROP ROLE "Role with a double quote """;

REVOKE ALL ON TABLE test_table FROM "Role with double ""quotes""";
DROP ROLE "Role with double ""quotes""";

REVOKE ALL ON TABLE test_table FROM "Role_""_with_""""_different' quotes''";
DROP ROLE "Role_""_with_""""_different' quotes''";

DROP TABLE test_table;
