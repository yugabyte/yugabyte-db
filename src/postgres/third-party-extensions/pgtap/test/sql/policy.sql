\unset ECHO
\i test/setup.sql

SELECT plan(180);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;
CREATE TABLE public.passwd(
    user_name  TEXT UNIQUE NOT NULL,
    pwhash     TEXT NOT NULL,
    uid        INT PRIMARY KEY,
    gid        INT NOT NULL,
    real_name  TEXT NOT NULL,
    home_phone TEXT,
    extra_info TEXT,
    home_dir   TEXT NOT NULL,
    shell      TEXT NOT NULL
);

CREATE ROLE root;
CREATE ROLE bob;
CREATE ROLE alice;
CREATE ROLE daemon;

INSERT INTO public.passwd VALUES
  ('admin','xxx',0,0,'Admin','111-222-3333',null,'/root','/bin/dash');
INSERT INTO public.passwd VALUES
  ('bob','xxx',1,1,'Bob','123-456-7890',null,'/home/bob','/bin/zsh');
INSERT INTO public.passwd VALUES
  ('alice','xxx',2,1,'Alice','098-765-4321',null,'/home/alice','/bin/zsh');
INSERT INTO public.passwd VALUES
  ('daemon','xxx',3,2,'daemon server',null,null,'/var/lib/daemon','/usr/sbin/nologin');

ALTER TABLE public.passwd ENABLE ROW LEVEL SECURITY;

-- Create policies
-- Administrator can see all rows and add any rows
CREATE POLICY root_all ON public.passwd TO root USING (true) WITH CHECK (true);
-- Normal users can view all rows
CREATE POLICY all_view ON public.passwd FOR SELECT TO root, bob, daemon USING (true);
-- Normal users can update their own records, but
-- limit which shells a normal user is allowed to set
CREATE POLICY user_mod ON public.passwd FOR UPDATE
  USING (current_user = user_name)
  WITH CHECK (
    current_user = user_name AND
    shell IN ('/bin/bash','/bin/sh','/bin/dash','/bin/zsh','/bin/tcsh')
  );
-- Daemons can insert and delete records with gid > 100
CREATE POLICY daemon_insert ON public.passwd FOR INSERT TO daemon WITH CHECK (gid > 100);
CREATE POLICY daemon_delete ON public.passwd FOR DELETE TO daemon USING (gid > 100);

RESET client_min_messages;

/****************************************************************************/
-- Test policies_are().
SELECT * FROM check_test(
    policies_are( 'public', 'passwd', ARRAY['root_all', 'all_view', 'user_mod', 'daemon_insert', 'daemon_delete'], 'whatever' ),
    true,
    'policies_are(schema, table, policies, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    policies_are( 'public', 'passwd', ARRAY['root_all', 'all_view', 'user_mod', 'daemon_insert', 'daemon_delete'] ),
    true,
    'policies_are(schema, table, policies)',
    'Table public.passwd should have the correct policies',
    ''
);

SELECT * FROM check_test(
    policies_are( 'public', 'passwd', ARRAY['all_view', 'user_mod', 'daemon_insert', 'daemon_delete'] ),
    false,
    'policies_are(schema, table, policies) + extra',
    'Table public.passwd should have the correct policies',
    '    Extra policies:
        root_all'
);

SELECT * FROM check_test(
    policies_are( 'public', 'passwd', ARRAY['root_all', 'all_view', 'user_mod', 'daemon_insert', 'daemon_delete', 'extra_policy'] ),
    false,
    'policies_are(schema, table, policies) + missing',
    'Table public.passwd should have the correct policies',
    '    Missing policies:
        extra_policy'
);

SELECT * FROM check_test(
    policies_are( 'public', 'passwd', ARRAY['all_view', 'user_mod', 'daemon_insert', 'daemon_delete', 'extra_policy'] ),
    false,
    'policies_are(schema, table, policies) + extra & missing',
    'Table public.passwd should have the correct policies',
    '    Extra policies:
        root_all
    Missing policies:
        extra_policy'
);

SELECT * FROM check_test(
    policies_are( 'passwd', ARRAY['root_all', 'all_view', 'user_mod', 'daemon_insert', 'daemon_delete'], 'whatever' ),
    true,
    'policies_are(table, policies, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    policies_are( 'passwd', ARRAY['root_all', 'all_view', 'user_mod', 'daemon_insert', 'daemon_delete'] ),
    true,
    'policies_are(table, policies)',
    'Table passwd should have the correct policies',
    ''
);

SELECT * FROM check_test(
    policies_are( 'passwd', ARRAY['all_view', 'user_mod', 'daemon_insert', 'daemon_delete'] ),
    false,
    'policies_are(table, policies) + extra',
    'Table passwd should have the correct policies',
    '    Extra policies:
        root_all'
);

SELECT * FROM check_test(
    policies_are( 'passwd', ARRAY['root_all', 'all_view', 'user_mod', 'daemon_insert', 'daemon_delete', 'extra_policy'] ),
    false,
    'policies_are(table, policies) + missing',
    'Table passwd should have the correct policies',
    '    Missing policies:
        extra_policy'
);

SELECT * FROM check_test(
    policies_are( 'passwd', ARRAY['all_view', 'user_mod', 'daemon_insert', 'daemon_delete', 'extra_policy'] ),
    false,
    'policies_are(table, policies) + extra & missing',
    'Table passwd should have the correct policies',
    '    Extra policies:
        root_all
    Missing policies:
        extra_policy'
);

/****************************************************************************/
-- Test policy_roles_are().
SELECT * FROM check_test(
    policy_roles_are( 'public', 'passwd', 'all_view', ARRAY['root', 'bob', 'daemon'], 'whatever' ),
    true,
    'policy_roles_are(schema, table, policy, roles, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    policy_roles_are( 'public', 'passwd', 'all_view', ARRAY['root', 'bob', 'daemon'] ),
    true,
    'policy_roles_are(schema, table, policy, roles)',
    'Policy all_view for table public.passwd should have the correct roles',
    ''
);

SELECT * FROM check_test(
    policy_roles_are( 'public', 'passwd', 'all_view', ARRAY['bob', 'daemon'] ),
    false,
    'policy_roles_are(schema, table, policy, roles) + extra',
    'Policy all_view for table public.passwd should have the correct roles',
    '    Extra policy roles:
        root'
);

SELECT * FROM check_test(
    policy_roles_are( 'public', 'passwd', 'all_view', ARRAY['root', 'bob', 'daemon', 'alice'] ),
    false,
    'policy_roles_are(schema, table, policy, roles) + missing',
    'Policy all_view for table public.passwd should have the correct roles',
    '    Missing policy roles:
        alice'
);

SELECT * FROM check_test(
    policy_roles_are( 'public', 'passwd', 'all_view', ARRAY['bob', 'daemon', 'alice'] ),
    false,
    'policy_roles_are(schema, table, policy, roles) + extra & missing',
    'Policy all_view for table public.passwd should have the correct roles',
    '    Extra policy roles:
        root
    Missing policy roles:
        alice'
);

SELECT * FROM check_test(
    policy_roles_are( 'passwd', 'all_view', ARRAY['root', 'bob', 'daemon'], 'whatever' ),
    true,
    'policy_roles_are(table, policy, roles, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    policy_roles_are( 'passwd', 'all_view', ARRAY['root', 'bob', 'daemon'] ),
    true,
    'policy_roles_are(table, policy, roles)',
    'Policy all_view for table passwd should have the correct roles',
    ''
);

SELECT * FROM check_test(
    policy_roles_are( 'passwd', 'all_view', ARRAY['bob', 'daemon'] ),
    false,
    'policy_roles_are(table, policy, roles) + extra',
    'Policy all_view for table passwd should have the correct roles',
    '    Extra policy roles:
        root'
);

SELECT * FROM check_test(
    policy_roles_are( 'passwd', 'all_view', ARRAY['root', 'bob', 'daemon', 'alice'] ),
    false,
    'policy_roles_are(table, policy, roles) + missing',
    'Policy all_view for table passwd should have the correct roles',
    '    Missing policy roles:
        alice'
);

SELECT * FROM check_test(
    policy_roles_are( 'passwd', 'all_view', ARRAY['bob', 'daemon', 'alice'] ),
    false,
    'policy_roles_are(table, policy, roles) + extra & missing',
    'Policy all_view for table passwd should have the correct roles',
    '    Extra policy roles:
        root
    Missing policy roles:
        alice'
);

/****************************************************************************/
-- Test policy_cmd_is().
SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'all_view', 'select', 'whatever' ),
    true,
    'policy_cmd_is(schema, table, policy, command, desc) for SELECT',
    'whatever',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'all_view'::NAME, 'select' ),
    true,
    'policy_cmd_is(schema, table, policy, command) for SELECT',
    'Policy all_view for table public.passwd should apply to SELECT command',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'all_view', 'select', 'whatever' ),
    true,
    'policy_cmd_is(table, policy, command, desc) for SELECT',
    'whatever',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'all_view', 'select' ),
    true,
    'policy_cmd_is(table, policy, command) for SELECT',
    'Policy all_view for table passwd should apply to SELECT command',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'all_view', 'delete', 'whatever' ),
    false,
    'policy_cmd_is(schema, table, policy, command, desc) for SELECT should fail',
    'whatever',
    '        have: SELECT
        want: DELETE'
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'all_view'::NAME, 'delete' ),
    false,
    'policy_cmd_is(schema, table, policy, command) for SELECT should fail',
    'Policy all_view for table public.passwd should apply to DELETE command',
    '        have: SELECT
        want: DELETE'
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'all_view', 'delete', 'whatever' ),
    false,
    'policy_cmd_is(table, policy, command, desc) for SELECT should fail',
    'whatever',
    '        have: SELECT
        want: DELETE'
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'all_view', 'delete' ),
    false,
    'policy_cmd_is(table, policy, command) for SELECT should fail',
    'Policy all_view for table passwd should apply to DELETE command',
    '        have: SELECT
        want: DELETE'
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'daemon_insert', 'insert', 'whatever' ),
    true,
    'policy_cmd_is(schema, table, policy, command, desc) for INSERT',
    'whatever',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'daemon_insert'::NAME, 'insert' ),
    true,
    'policy_cmd_is(schema, table, policy, command) for INSERT',
    'Policy daemon_insert for table public.passwd should apply to INSERT command',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'daemon_insert', 'insert', 'whatever' ),
    true,
    'policy_cmd_is(table, policy, command, desc) for INSERT',
    'whatever',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'daemon_insert', 'insert' ),
    true,
    'policy_cmd_is(table, policy, command) for INSERT',
    'Policy daemon_insert for table passwd should apply to INSERT command',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'daemon_insert', 'delete', 'whatever' ),
    false,
    'policy_cmd_is(schema, table, policy, command, desc) for INSERT should fail',
    'whatever',
    '        have: INSERT
        want: DELETE'
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'daemon_insert'::NAME, 'delete' ),
    false,
    'policy_cmd_is(schema, table, policy, command) for INSERT should fail',
    'Policy daemon_insert for table public.passwd should apply to DELETE command',
    '        have: INSERT
        want: DELETE'
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'daemon_insert', 'delete', 'whatever' ),
    false,
    'policy_cmd_is(table, policy, command, desc) for INSERT should fail',
    'whatever',
    '        have: INSERT
        want: DELETE'
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'daemon_insert', 'delete' ),
    false,
    'policy_cmd_is(table, policy, command) for INSERT should fail',
    'Policy daemon_insert for table passwd should apply to DELETE command',
    '        have: INSERT
        want: DELETE'
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'user_mod', 'update', 'whatever' ),
    true,
    'policy_cmd_is(schema, table, policy, command, desc) for UPDATE',
    'whatever',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'user_mod'::NAME, 'update' ),
    true,
    'policy_cmd_is(schema, table, policy, command) for UPDATE',
    'Policy user_mod for table public.passwd should apply to UPDATE command',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'user_mod', 'update', 'whatever' ),
    true,
    'policy_cmd_is(table, policy, command, desc) for UPDATE',
    'whatever',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'user_mod', 'update' ),
    true,
    'policy_cmd_is(table, policy, command) for UPDATE',
    'Policy user_mod for table passwd should apply to UPDATE command',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'user_mod', 'delete', 'whatever' ),
    false,
    'policy_cmd_is(schema, table, policy, command, desc) for UPDATE should fail',
    'whatever',
    '        have: UPDATE
        want: DELETE'
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'user_mod'::NAME, 'all' ),
    false,
    'policy_cmd_is(schema, table, policy, command) for UPDATE should fail',
    'Policy user_mod for table public.passwd should apply to ALL command',
    '        have: UPDATE
        want: ALL'
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'user_mod', 'all', 'whatever' ),
    false,
    'policy_cmd_is(table, policy, command, desc) for UPDATE should fail',
    'whatever',
    '        have: UPDATE
        want: ALL'
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'user_mod', 'all' ),
    false,
    'policy_cmd_is(table, policy, command) for UPDATE should fail',
    'Policy user_mod for table passwd should apply to ALL command',
    '        have: UPDATE
        want: ALL'
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'daemon_delete', 'delete', 'whatever' ),
    true,
    'policy_cmd_is(schema, table, policy, command, desc) for DELETE',
    'whatever',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'daemon_delete'::NAME, 'delete' ),
    true,
    'policy_cmd_is(schema, table, policy, command) for DELETE',
    'Policy daemon_delete for table public.passwd should apply to DELETE command',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'daemon_delete', 'delete', 'whatever' ),
    true,
    'policy_cmd_is(table, policy, command, desc) for DELETE',
    'whatever',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'daemon_delete', 'delete' ),
    true,
    'policy_cmd_is(table, policy, command) for DELETE',
    'Policy daemon_delete for table passwd should apply to DELETE command',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'daemon_delete', 'all', 'whatever' ),
    false,
    'policy_cmd_is(schema, table, policy, command, desc) for DELETE should fail',
    'whatever',
    '        have: DELETE
        want: ALL'
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'daemon_delete'::NAME, 'all' ),
    false,
    'policy_cmd_is(schema, table, policy, command) for DELETE should fail',
    'Policy daemon_delete for table public.passwd should apply to ALL command',
    '        have: DELETE
        want: ALL'
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'daemon_delete', 'all', 'whatever' ),
    false,
    'policy_cmd_is(table, policy, command, desc) for DELETE should fail',
    'whatever',
    '        have: DELETE
        want: ALL'
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'daemon_delete', 'all' ),
    false,
    'policy_cmd_is(table, policy, command) for DELETE should fail',
    'Policy daemon_delete for table passwd should apply to ALL command',
    '        have: DELETE
        want: ALL'
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'root_all', 'all', 'whatever' ),
    true,
    'policy_cmd_is(schema, table, policy, command, desc) for ALL',
    'whatever',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'root_all'::NAME, 'all' ),
    true,
    'policy_cmd_is(schema, table, policy, command) for ALL',
    'Policy root_all for table public.passwd should apply to ALL command',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'root_all', 'all', 'whatever' ),
    true,
    'policy_cmd_is(table, policy, command, desc) for ALL',
    'whatever',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'root_all', 'all' ),
    true,
    'policy_cmd_is(table, policy, command) for ALL',
    'Policy root_all for table passwd should apply to ALL command',
    ''
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'root_all', 'delete', 'whatever' ),
    false,
    'policy_cmd_is(schema, table, policy, command, desc) for ALL should fail',
    'whatever',
    '        have: ALL
        want: DELETE'
);

SELECT * FROM check_test(
    policy_cmd_is( 'public', 'passwd', 'root_all'::NAME, 'delete' ),
    false,
    'policy_cmd_is(schema, table, policy, command) for ALL should fail',
    'Policy root_all for table public.passwd should apply to DELETE command',
    '        have: ALL
        want: DELETE'
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'root_all', 'delete', 'whatever' ),
    false,
    'policy_cmd_is(table, policy, command, desc) for ALL should fail',
    'whatever',
    '        have: ALL
        want: DELETE'
);

SELECT * FROM check_test(
    policy_cmd_is( 'passwd', 'root_all', 'delete' ),
    false,
    'policy_cmd_is(table, policy, command) for ALL should fail',
    'Policy root_all for table passwd should apply to DELETE command',
    '        have: ALL
        want: DELETE'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
