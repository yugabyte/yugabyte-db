BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

CREATE TABLE owner AS SELECT 'Paul' AS firstname;

SECURITY LABEL FOR anon ON COLUMN owner.firstname
  IS 'MASKED WITH VALUE ''Robert'' ';

CREATE TABLE company (
  id SERIAL PRIMARY KEY,
  name TEXT,
  vat_id TEXT UNIQUE
);

INSERT INTO company
VALUES
(952,'Shadrach', 'FR62684255667'),
(194,E'Johnny\'s Shoe Store','CHE670945644'),
(346,'Capitol Records','GB663829617823')
;

CREATE TABLE supplier (
  id SERIAL PRIMARY KEY,
  fk_company_id INT REFERENCES company(id),
  contact TEXT,
  phone TEXT,
  job_title TEXT
);

INSERT INTO supplier
VALUES
(299,194,'Johnny Ryall','597-500-569','CEO'),
(157,346,'George Clinton', '131-002-530','Sales manager')
;

CREATE TABLE http_logs (
  id integer NOT NULL,
  ip_address INET,
  url TEXT
);

INSERT INTO http_logs
VALUES
(1, '178.83.218.166', 'http://bluehost.com'),
(2, '24.54.240.199', 'https://yandex.ru'),
(3, '26.101.173.120', 'https://cbsnews.com'),
(4, '79.134.42.230', 'http://microsoft.com'),
(5, '181.202.212.164', 'http://studiopress.com'),
(6, '48.131.203.247', 'https://bloglines.com'),
(7, '109.57.63.38', 'http://google.ru'),
(8, '91.243.96.208', 'https://state.tx.us'),
(9, '193.190.39.5', 'https://princeton.edu'),
(10, '81.30.58.12', 'https://forbes.com'),
(11, '177.24.171.6', 'https://psu.edu'),
(12, '138.73.192.228', 'http://cargocollective.com'),
(13, '41.53.124.171', 'https://senate.gov'),
(14, '32.172.199.234', 'http://ed.gov'),
(15, '126.56.189.28', 'http://mozilla.org'),
(16, '19.26.123.205', 'http://feedburner.com'),
(17, '56.255.149.74', 'http://newsvine.com'),
(18, '205.246.209.63', 'https://constantcontact.com'),
(19, '125.242.92.8', 'http://latimes.com'),
(20, '99.21.187.67', 'https://cornell.edu'),
(21, '196.114.209.22', 'https://hostgator.com'),
(22, '249.17.78.122', 'http://indiatimes.com'),
(23, '29.189.152.157', 'https://typepad.com'),
(24, '180.58.82.197', 'https://ftc.gov'),
(25, '58.81.31.167', 'http://businesswire.com'),
(26, '194.16.44.36', 'https://rakuten.co.jp'),
(27, '124.78.160.215', 'https://hibu.com'),
(28, '157.179.182.40', 'http://hibu.com'),
(29, '192.33.165.227', 'https://home.pl'),
(30, '0.60.17.105', 'https://wikia.com'),
(31, '211.110.45.67', 'https://adobe.com'),
(32, '91.199.155.90', 'http://google.com.br'),
(33, '39.215.114.86', 'https://fc2.com'),
(34, '247.234.202.57', 'https://vk.com'),
(35, '182.207.200.251', 'http://amazon.com'),
(36, '208.251.81.143', 'https://drupal.org'),
(37, '12.196.146.197', 'http://hubpages.com'),
(38, '217.23.172.97', 'https://state.tx.us'),
(39, '89.19.11.170', 'http://ustream.tv'),
(40, '15.144.237.174', 'http://businesswire.com'),
(41, '108.245.184.91', 'http://yolasite.com'),
(42, '67.72.48.218', 'https://goo.ne.jp'),
(43, '91.20.141.140', 'http://youtube.com'),
(44, '76.35.210.226', 'https://t-online.de'),
(45, '195.247.120.240', 'http://ask.com'),
(46, '40.67.186.129', 'http://ihg.com'),
(47, '115.22.80.58', 'http://ebay.com'),
(48, '104.151.49.202', 'http://nps.gov'),
(49, '17.117.113.112', 'https://apache.org'),
(50, '86.231.162.109', 'https://tmall.com'),
(51, '250.102.66.241', 'http://vinaora.com'),
(52, '20.225.118.184', 'http://ustream.tv'),
(53, '21.109.222.231', 'http://phoca.cz'),
(54, '136.49.193.247', 'http://ustream.tv'),
(55, '94.155.224.179', 'http://shutterfly.com'),
(56, '224.106.255.172', 'http://eepurl.com'),
(57, '69.125.42.134', 'http://intel.com'),
(58, '203.149.36.154', 'https://hao123.com'),
(59, '62.118.236.14', 'https://arstechnica.com'),
(60, '129.8.223.46', 'http://ow.ly'),
(61, '94.230.172.86', 'https://barnesandnoble.com'),
(62, '101.203.169.158', 'http://wp.com'),
(63, '239.188.143.121', 'http://newyorker.com'),
(64, '159.127.74.95', 'http://flickr.com'),
(65, '114.226.147.77', 'https://ucoz.com'),
(66, '148.25.93.199', 'https://ameblo.jp'),
(67, '255.91.125.31', 'http://mit.edu'),
(68, '74.143.18.177', 'https://usgs.gov'),
(69, '199.53.99.240', 'https://google.com.br'),
(70, '197.180.238.154', 'http://acquirethisname.com'),
(71, '58.1.68.195', 'http://surveymonkey.com'),
(72, '108.233.23.96', 'http://vkontakte.ru'),
(73, '197.177.168.123', 'http://list-manage.com'),
(74, '240.34.236.123', 'http://biglobe.ne.jp'),
(75, '56.201.201.170', 'https://cocolog-nifty.com'),
(76, '8.191.189.135', 'http://sphinn.com'),
(77, '197.230.182.61', 'http://marketwatch.com'),
(78, '94.25.158.59', 'http://businessweek.com'),
(79, '102.221.163.253', 'http://webeden.co.uk'),
(80, '46.244.182.93', 'http://taobao.com'),
(81, '245.103.51.232', 'http://nature.com'),
(82, '142.219.100.135', 'http://rakuten.co.jp'),
(83, '249.128.248.178', 'https://phpbb.com'),
(84, '117.94.158.86', 'http://vk.com'),
(85, '49.169.82.156', 'http://tripod.com'),
(86, '140.236.242.149', 'http://ezinearticles.com'),
(87, '45.174.23.157', 'https://upenn.edu'),
(88, '115.130.176.201', 'https://guardian.co.uk'),
(89, '184.248.173.101', 'http://behance.net'),
(90, '85.233.80.180', 'https://youku.com'),
(91, '48.163.77.88', 'http://amazon.de'),
(92, '202.230.111.183', 'https://issuu.com'),
(93, '34.175.234.98', 'http://phoca.cz'),
(94, '241.242.27.211', 'https://sina.com.cn'),
(95, '157.234.126.172', 'https://t.co'),
(96, '9.243.27.97', 'https://goo.ne.jp'),
(97, '225.94.78.148', 'https://goo.gl'),
(98, '64.34.102.161', 'http://howstuffworks.com'),
(99, '14.63.189.172', 'https://histats.com'),
(100, '154.247.8.231', 'https://fotki.com');

CREATE SCHEMA test_pg_dump_anon;

CREATE TABLE test_pg_dump_anon.no_masks AS SELECT 1 ;

CREATE SEQUENCE test_pg_dump_anon.three
INCREMENT -1
MINVALUE 1
MAXVALUE 3
START 3
CYCLE;

CREATE SEQUENCE public.seq42;
ALTER SEQUENCE public.seq42 RESTART WITH 42;

CREATE SCHEMA "FoO";

CREATE SEQUENCE "FoO"."BuG_298";
ALTER SEQUENCE "FoO"."BuG_298" RESTART WITH 298;

COMMIT;
