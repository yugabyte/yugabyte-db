\set ON_ERROR_STOP on

CREATE TABLE test_table(id INT PRIMARY KEY);
INSERT INTO test_table (id) VALUES (1), (2), (3);

CREATE ROLE admin LOGIN NOINHERIT;
REVOKE ALL ON TABLE test_table FROM admin;
GRANT SELECT ON TABLE test_table TO admin;

CREATE ROLE "CaseSensitiveRole" LOGIN NOINHERIT;
REVOKE ALL ON TABLE test_table FROM "CaseSensitiveRole";
GRANT SELECT ON TABLE test_table TO "CaseSensitiveRole";

CREATE ROLE "role_with_a space" LOGIN NOINHERIT;
REVOKE ALL ON TABLE test_table FROM "role_with_a space";
GRANT SELECT ON TABLE test_table TO "role_with_a space";

CREATE ROLE "Role with spaces" LOGIN NOINHERIT;
REVOKE ALL ON TABLE test_table FROM "Role with spaces";
GRANT SELECT ON TABLE test_table TO "Role with spaces";

CREATE ROLE "Role with a quote '" LOGIN NOINHERIT;
REVOKE ALL ON TABLE test_table FROM "Role with a quote '";
GRANT SELECT ON TABLE test_table TO "Role with a quote '";

CREATE ROLE "Role with 'quotes'" LOGIN NOINHERIT;
REVOKE ALL ON TABLE test_table FROM "Role with 'quotes'";
GRANT SELECT ON TABLE test_table TO "Role with 'quotes'";

CREATE ROLE "Role with a double quote """ LOGIN NOINHERIT;
REVOKE ALL ON TABLE test_table FROM "Role with a double quote """;
GRANT SELECT ON TABLE test_table TO "Role with a double quote """;

CREATE ROLE "Role with double ""quotes""" LOGIN NOINHERIT;
REVOKE ALL ON TABLE test_table FROM "Role with double ""quotes""";
GRANT SELECT ON TABLE test_table TO "Role with double ""quotes""";

CREATE ROLE "Role_""_with_""""_different' quotes''" LOGIN NOINHERIT;
REVOKE ALL ON TABLE test_table FROM "Role_""_with_""""_different' quotes''";
GRANT SELECT ON TABLE test_table TO "Role_""_with_""""_different' quotes''";
