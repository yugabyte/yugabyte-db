\unset ECHO
\i test/setup.sql

SELECT plan(60);
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
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
