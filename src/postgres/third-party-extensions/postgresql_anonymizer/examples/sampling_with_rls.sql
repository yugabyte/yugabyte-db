BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SET anon.transparent_dynamic_masking = TRUE;

CREATE TABLE public.access_logs (
  date_open TIMESTAMP,
  session_id TEXT,
  ip_addr INET,
  url TEXT,
  browser TEXT DEFAULT 'unkown',
  operating_system TEXT DEFAULT NULL,
  locale TEXT
);

SECURITY LABEL FOR anon ON COLUMN public.access_logs.ip_addr
  IS 'MASKED WITH VALUE NULL';

INSERT INTO public.access_logs VALUES
('2023-01-15 08:23:45', 'sess_2301', '172.16.0.100', '/products.html', 'Firefox/108.0', 'MacOS', 'en_GB'),
('2023-02-01 12:34:56', 'sess_4521', '10.0.0.123', '/cart.html', 'Chrome/109.0', 'Windows', 'es_ES'),
('2023-02-15 15:45:12', 'sess_7845', '192.168.1.45', '/checkout.html', 'Safari/15.0', 'iOS', 'fr_FR'),
('2023-03-01 09:15:30', 'sess_9632', '172.20.0.89', '/about.html', 'Edge/110.0', 'Windows', 'de_DE'),
('2023-03-15 14:20:18', 'sess_1478', '10.10.0.234', '/contact.html', 'Chrome/110.0', 'Android', 'it_IT'),
('2023-04-01 11:30:45', 'sess_3698', '192.168.2.78', '/blog.html', 'Firefox/109.0', 'Linux', 'pt_BR'),
('2023-04-15 16:40:22', 'sess_8520', '172.18.0.156', '/faq.html', 'Safari/15.1', 'MacOS', 'ja_JP'),
('2023-05-01 10:25:33', 'sess_7410', '10.20.0.167', '/support.html', 'Chrome/111.0', 'Windows', 'ko_KR'),
('2023-05-15 13:50:41', 'sess_9630', '192.168.3.89', '/news.html', 'Edge/111.0', 'Windows', 'zh_CN'),
('2023-06-01 09:45:15', 'sess_1597', '172.17.0.223', '/services.html', 'Firefox/110.0', 'Linux', 'ru_RU'),
('2023-06-15 15:30:28', 'sess_3574', '10.30.0.198', '/login.html', 'Chrome/112.0', 'Android', 'nl_NL'),
('2023-07-01 12:20:39', 'sess_7532', '192.168.4.167', '/register.html', 'Safari/15.2', 'iOS', 'sv_SE'),
('2023-07-15 14:15:52', 'sess_9514', '172.19.0.145', '/profile.html', 'Edge/112.0', 'Windows', 'pl_PL'),
('2023-08-01 11:40:33', 'sess_2583', '10.40.0.178', '/settings.html', 'Firefox/111.0', 'MacOS', 'tr_TR'),
('2023-08-15 16:55:47', 'sess_4561', '192.168.5.234', '/help.html', 'Chrome/113.0', 'Windows', 'ar_SA'),
('2023-09-01 10:30:19', 'sess_7892', '172.16.0.167', '/search.html', 'Safari/15.3', 'MacOS', 'hi_IN'),
('2023-09-15 13:45:28', 'sess_1235', '10.50.0.145', '/categories.html', 'Edge/113.0', 'Windows', 'th_TH'),
('2023-10-01 15:20:36', 'sess_4567', '192.168.6.198', '/orders.html', 'Firefox/112.0', 'Linux', 'vi_VN'),
('2023-10-15 09:35:42', 'sess_7890', '172.20.0.178', '/wishlist.html', 'Chrome/114.0', 'Android', 'cs_CZ'),
('2023-11-01 12:50:55', 'sess_1472', '10.60.0.223', '/newsletter.html', 'Safari/15.4', 'iOS', 'hu_HU'),
('2023-11-15 14:40:23', 'sess_3698', '192.168.7.145', '/privacy.html', 'Edge/114.0', 'Windows', 'el_GR'),
('2023-12-01 11:25:34', 'sess_7536', '172.18.0.198', '/terms.html', 'Firefox/113.0', 'MacOS', 'ro_RO'),
('2023-12-15 16:30:47', 'sess_9512', '10.70.0.167', '/sitemap.html', 'Chrome/115.0', 'Windows', 'sk_SK'),
('2024-01-01 10:15:29', 'sess_2585', '192.168.8.178', '/careers.html', 'Safari/15.5', 'MacOS', 'uk_UA'),
('2024-01-15 13:40:38', 'sess_4563', '172.17.0.145', '/partners.html', 'Edge/115.0', 'Windows', 'bg_BG'),
('2024-02-01 15:55:46', 'sess_7894', '10.80.0.234', '/downloads.html', 'Firefox/114.0', 'Linux', 'sr_RS'),
('2024-02-15 09:20:33', 'sess_1237', '192.168.9.167', '/gallery.html', 'Chrome/116.0', 'Android', 'hr_HR'),
('2024-03-01 12:35:42', 'sess_4569', '172.19.0.198', '/events.html', 'Safari/15.6', 'iOS', 'sl_SI'),
('2024-03-15 14:50:55', 'sess_7892', '10.90.0.178', '/subscribe.html', 'Edge/116.0', 'Windows', 'et_EE'),
('2024-04-01 11:40:23', 'sess_1474', '192.168.10.145', '/feedback.html', 'Firefox/115.0', 'MacOS', 'lv_LV'),
('2024-04-15 16:25:34', 'sess_3700', '172.16.0.223', '/shipping.html', 'Chrome/117.0', 'Windows', 'lt_LT'),
('2024-05-01 10:30:47', 'sess_7538', '10.100.0.167', '/returns.html', 'Safari/15.7', 'MacOS', 'fi_FI'),
('2024-05-15 13:15:29', 'sess_9514', '192.168.11.198', '/track.html', 'Edge/117.0', 'Windows', 'he_IL'),
('2024-06-01 15:40:38', 'sess_2587', '172.20.0.178', '/gifting.html', 'Firefox/116.0', 'Linux', 'id_ID'),
('2024-06-15 09:55:46', 'sess_4565', '10.110.0.145', '/brands.html', 'Chrome/118.0', 'Android', 'ms_MY'),
('2024-07-01 12:20:33', 'sess_7896', '192.168.12.234', '/deals.html', 'Safari/16.0', 'iOS', 'bn_IN'),
('2024-07-15 14:35:42', 'sess_1239', '172.18.0.167', '/trending.html', 'Edge/118.0', 'Windows', 'ka_GE'),
('2024-08-01 11:50:55', 'sess_4571', '10.120.0.198', '/featured.html', 'Firefox/117.0', 'MacOS', 'hy_AM'),
('2024-08-15 16:40:23', 'sess_7894', '192.168.13.178', '/seasonal.html', 'Chrome/119.0', 'Windows', 'az_AZ'),
('2024-09-01 10:25:34', 'sess_1476', '172.17.0.145', '/clearance.html', 'Safari/16.1', 'MacOS', 'kk_KZ'),
('2024-09-15 13:30:47', 'sess_3702', '10.130.0.223', '/outlet.html', 'Edge/119.0', 'Windows', 'uz_UZ'),
('2024-10-01 15:15:29', 'sess_7540', '192.168.14.167', '/premium.html', 'Firefox/118.0', 'Linux', 'mn_MN'),
('2024-10-15 09:40:38', 'sess_9516', '172.19.0.198', '/exclusive.html', 'Chrome/120.0', 'Android', 'sq_AL'),
('2024-11-01 12:55:46', 'sess_2589', '10.140.0.178', '/membership.html', 'Safari/16.2', 'iOS', 'mk_MK'),
('2024-11-15 14:20:33', 'sess_4567', '192.168.15.145', '/rewards.html', 'Edge/120.0', 'Windows', 'bs_BA'),
('2024-12-01 11:35:42', 'sess_7898', '172.16.0.234', '/loyalty.html', 'Firefox/119.0', 'MacOS', 'mt_MT'),
('2024-12-15 16:50:55', 'sess_1241', '10.150.0.167', '/community.html', 'Chrome/121.0', 'Windows', 'is_IS'),
('2025-01-01 10:40:23', 'sess_4573', '192.168.16.198', '/forum.html', 'Safari/16.3', 'MacOS', 'cy_GB'),
('2025-01-15 13:25:34', 'sess_7896', '172.20.0.178', '/blog/posts.html', 'Edge/121.0', 'Windows', 'eu_ES'),
('2025-02-01 15:30:47', 'sess_1478', '10.160.0.145', '/blog/comments.html', 'Firefox/120.0', 'Linux', 'gl_ES');


--
-- Create 2 users : a masked role and a regular role
--

CREATE ROLE regis;
GRANT pg_read_all_data TO regis;

CREATE ROLE marc;
SECURITY LABEL FOR anon ON ROLE marc IS 'MASKED';
GRANT pg_read_all_data TO marc;

--
-- Define a Row Level Security policy that will be applied only for masked user
--
ALTER TABLE public.access_logs ENABLE ROW LEVEL SECURITY;

CREATE POLICY access_logs_sampling_for_masked_users
ON public.access_logs
USING (
  NOT anon.hasmask(CURRENT_USER::REGROLE)
--  OR date_open >= '2024-01-01'::DATE
  OR date_open >= now()- '6 months'::INTERVAL
);

--
-- Regis can see all the log entries
--
SET ROLE regis;

SELECT * FROM public.access_logs ORDER BY date_open LIMIT 1;

SELECT count(*) FROM public.access_logs;

--
-- Marc is masked (ip_address is null) and sees a subset of the table
--
SET ROLE marc;

SELECT * FROM public.access_logs ORDER BY date_open LIMIT 1;

SELECT count(*) FROM public.access_logs;

RESET ROLE;

ROLLBACK;

