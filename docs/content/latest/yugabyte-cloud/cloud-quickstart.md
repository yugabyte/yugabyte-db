---
title: Quickstart
linkTitle: Quickstart
description: Use a Free cluster to get started using YugabyteDB, explore core features, and develop applications.
headcontent:
image: /images/section_icons/index/quick_start.png
aliases:
  - /latest/deploy/yugabyte-cloud/free-tier/
  - /latest/yugabyte-cloud/free-tier/
menu:
  latest:
    identifier: cloud-quickstart
    parent: yugabyte-cloud
    weight: 10
isTocNested: true
showAsideToc: true
---

The quickest way to get started with Yugabyte Cloud is to set up a free cluster. Although not suitable for production workloads, the free cluster includes enough resources to learn and develop applications with YugabyteDB.

You can get started with Yugabyte Cloud by setting up a free cluster in a few simple steps:

1. Sign up.
1. Add a free cluster.
1. Connect to the cluster.
1. Create a database and load data.
1. Explore YugabyteDB.

## Sign up

1. Go to <http://cloud.yugabyte.com>. 
    
1. Enter your details (name, email, and create a password), and click **Start using Yugabyte Cloud**.

1. Verify your email address by clicking the link in the verification email sent to your email address.

1. Sign in using your email address and the password you created.

Once the Sign up process completes, the **Getting Started** page of the Yugabyte Cloud console is displayed.

## Add a free cluster

1. On the **Getting Started** page, click **Create a free cluster** to open the **Create Cluster** wizard. 

1. Select **Yugabyte Cloud Free** and click **Next**.

1. Choose the provider (AWS or GCP), enter a name for the cluster, and choose the region, then click **Next**.

1. Choose the credentials you'll use to connect to your YugabyteDB database in the cloud. You can choose the default set with a database user named "admin", or create your own.

1. Click **Download credentials** and save your credentials in a secure location.

1. Verify that your credentials are downloaded, and click **Create Cluster**.

Once you complete the wizard, Yugabyte Cloud bootstraps and provisions the cluster, and configures YugabyteDB. The process takes up to 15 minutes.

Once the cluster is ready, the cluster [Overview](../cloud-clusters/overview) is displayed. You now have a fully configured YugabyteDB cluster provisioned in Yugabyte Cloud.

## Connect to your cluster using the cloud shell

Once the **Overview** tab is displayed, you can connect to the cluster.

Using the cloud shell, you can connect to and interact with your YugabyteDB database from your browser. You have the option of using the following CLIs in the cloud shell:

- [ysqlsh](../../admin/ysqlsh/) - YSQL shell for interacting with YugabyteDB using the [YSQL API](../../api/ysql) 
- [ycqlsh](../../admin/ycqlsh/) - YCQL shell which uses the [YCQL API](../../api/ycql)

To use `ysqlsh` to create and manage YugabyteDB databases and tables in your Yugabyte Cloud cluster, do the following:

1. Click **Connect** to display the **Connect to Cluster** dialog.

1. Under **Cloud Shell**, click **Launch Cloud Shell**.

1. Enter the database name (`yugabyte`), the user name (`admin`), select the YSQL API and click **Confirm**.

    The cloud shell opens in a separate browser window. Cloud shell can take up to 30 seconds to be ready.

    ```output
    Password for user admin: 
    ```

1. Enter the password for the admin user that you saved previously.

The `ysqlsh` shell prompt appears and is ready to use.

```output
ysqlsh (11.2-YB-2.2.0.0-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.

yugabyte=#
```

## Create a database and load data

You can now create a database and load some sample data.

1. Create a database (`yb_demo`) by using the following `CREATE DATABASE` command.

    ```sql
    yugabyte=# CREATE DATABASE yb_demo;
    ```

1. Connect to the new database using the following YSQL shell `\c` meta command.

    ```sql
    yugabyte=# \c yb_demo;
    ```

1. Create the database schema, which includes four tables, by running the following commands.

    ```sql
    CREATE TABLE products(
      id         bigserial PRIMARY KEY,
      created_at timestamp,
      category   text,
      ean        text,
      price      float,
      quantity   int default(5000),
      rating     float,
      title      text,
      vendor     text
    );
    ```

    ```sql
    CREATE TABLE users(
      id         bigserial PRIMARY KEY,
      created_at timestamp,
      name       text,
      email      text,
      address    text,
      city       text,
      state      text,
      zip        text,
      birth_date text,
      latitude   float,
      longitude  float,
      password   text,
      source     text
    );
    ```

    ```sql
    CREATE TABLE orders(
      id         bigserial PRIMARY KEY,
      created_at timestamp,
      user_id    bigint,
      product_id bigint,
      discount   float,
      quantity   int,
      subtotal   float,
      tax        float,
      total      float
    );
    ```

    ```sql
    CREATE TABLE reviews(
      id         bigserial PRIMARY KEY,
      created_at timestamp,
      reviewer   text,
      product_id bigint,
      rating     int,
      body       text
    );
    ```

1. Load some data into the products table by running the following commands.

    ```sql
    yb_demo=# INSERT INTO products (id, category, created_at, ean, price, rating, title, vendor) VALUES (1,'Gizmo','2017-07-19T19:44:56.582Z',1018947080336,29.463261130679875,4.6,'Rustic Paper Wallet','Swaniawski, Casper and Hilll');
    INSERT INTO products (id, category, created_at, ean, price, rating, title, vendor) VALUES (2,'Doohickey','2019-04-11T08:49:35.932Z',7663515285824,70.07989613071763,0.0,'Small Marble Shoes','Balistreri-Ankunding');
    INSERT INTO products (id, category, created_at, ean, price, rating, title, vendor) VALUES (3,'Doohickey','2018-09-08T22:03:20.239Z',4966277046676,35.388744881539054,4.0,'Synergistic Granite Chair','Murray, Watsica and Wunsch');
    INSERT INTO products (id, category, created_at, ean, price, rating, title, vendor) VALUES (4,'Doohickey','2018-03-06T02:53:09.937Z',4134502155718,73.99178100854834,3.0,'Enormous Aluminum Shirt','Regan Bradtke and Sons');
    INSERT INTO products (id, category, created_at, ean, price, rating, title, vendor) VALUES (5,'Gadget','2016-10-03T01:47:39.147Z',5499736705597,82.7450976850356,4.0,'Enormous Marble Wallet','Price, Schultz and Daniel');
    INSERT INTO products (id, category, created_at, ean, price, rating, title, vendor) VALUES (6,'Doohickey','2017-03-29T05:43:40.150Z',2293343551454,64.95747510229587,3.8,'Small Marble Hat','Nolan-Wolff');
    INSERT INTO products (id, category, created_at, ean, price, rating, title, vendor) VALUES (7,'Doohickey','2017-06-03T03:07:28.061Z',0157967025871,98.81933684368194,4.3,'Aerodynamic Linen Coat','Little-Pagac');
    INSERT INTO products (id, category, created_at, ean, price, rating, title, vendor) VALUES (8,'Doohickey','2018-04-30T15:03:53.193Z',1078766578568,65.89215669329305,4.1,'Enormous Steel Watch','Senger-Stamm');
    INSERT INTO products (id, category, created_at, ean, price, rating, title, vendor) VALUES (9,'Widget','2019-02-07T08:26:25.647Z',7217466997444,58.31312098526137,4.2,'Practical Bronze Computer','Keely Stehr Group');
    INSERT INTO products (id, category, created_at, ean, price, rating, title, vendor) VALUES (10,'Gizmo','2017-01-09T09:51:20.352Z',1807963902339,31.78621880685793,4.3,'Mediocre Wooden Table','Larson, Pfeffer and Klocko');
    INSERT INTO products (id, category, created_at, ean, price, rating, title, vendor) VALUES (11,'Gadget','2018-05-28T08:02:54.482Z',3642408008706,88.30453275661709,0.0,'Ergonomic Silk Coat','Upton, Kovacek and Halvorson');
    INSERT INTO products (id, category, created_at, ean, price, rating, title, vendor) VALUES (12,'Gizmo','2017-11-12T14:51:16.221Z',9482467478850,77.34285054412217,4.4,'Sleek Paper Toucan','Mueller-Dare');
    INSERT INTO products (id, category, created_at, ean, price, rating, title, vendor) VALUES (13,'Gizmo','2016-05-24T23:09:46.392Z',0399569209871,75.0861692740371,0.0,'Synergistic Steel Chair','Mr. Tanya Stracke and Sons');
    INSERT INTO products (id, category, created_at, ean, price, rating, title, vendor) VALUES (14,'Widget','2017-12-31T14:41:56.870Z',8833419218504,25.09876359271891,4.0,'Awesome Concrete Shoes','McClure-Lockman');
    INSERT INTO products (id, category, created_at, ean, price, rating, title, vendor) VALUES (15,'Widget','2016-09-08T14:42:57.264Z',5881647583898,25.09876359271891,4.0,'Aerodynamic Paper Computer','Friesen-Anderson');
    ```

1. Load some data into the users table by running the following commands.

    ```sql
    yb_demo=# INSERT INTO users(created_at,source,name,city,birth_date,latitude,zip,password,id,longitude,address,state,email) VALUES ('2017-10-07T01:34:35.462Z','Twitter','Hudson Borer','Wood River','1986-12-12',40.71314890000001,68883,'ccca881f-3e4b-4e5c-8336-354103604af6',1,-98.5259864,'9611-9809 West Rosedale Road','NE','borer-hudson@yahoo.com');
    INSERT INTO users(created_at,source,name,city,birth_date,latitude,zip,password,id,longitude,address,state,email) VALUES ('2018-04-09T12:10:05.167Z','Affiliate','Domenica Williamson','Searsboro','1967-06-10',41.5813224,50242,'eafc45bf-cf8e-4c96-ab35-ce44d0021597',2,-92.6991321,'101 4th Street','IA','williamson-domenica@yahoo.com');
    INSERT INTO users(created_at,source,name,city,birth_date,latitude,zip,password,id,longitude,address,state,email) VALUES ('2017-06-27T06:06:20.625Z','Facebook','Lina Heaney','Sandstone','1961-12-18',46.11973039999999,55072,'36f67891-34e5-4439-a8a4-2d9246775ff8',3,-92.8416108,'29494 Anderson Drive','MN','lina.heaney@yahoo.com');
    INSERT INTO users(created_at,source,name,city,birth_date,latitude,zip,password,id,longitude,address,state,email) VALUES ('2019-02-21T13:59:15.348Z','Google','Arnold Adams','Rye','1992-08-12',37.9202933,81069,'537a727b-7525-44a3-99c8-8fdc488fbf02',4,-104.9726909,'2-7900 Cuerno Verde Road','CO','adams.arnold@gmail.com');
    INSERT INTO users(created_at,source,name,city,birth_date,latitude,zip,password,id,longitude,address,state,email) VALUES ('2017-09-05T03:36:44.811Z','Twitter','Dominique Leffler','Beaver Dams','1974-04-20',42.348954,14812,'6a802b6c-4da8-4881-9ca6-4f69085c7c14',5,-77.056681,'761 Fish Hill Road','NY','leffler.dominique@hotmail.com');
    INSERT INTO users(created_at,source,name,city,birth_date,latitude,zip,password,id,longitude,address,state,email) VALUES ('2016-09-22T10:08:29.599Z','Google','Rene Muller','Morse','1983-03-27',30.1514772,70559,'760495fb-9c38-4351-a6ee-4743d10d345e',6,-92.4861786,'1243 West Whitney Street','LA','rene.muller@gmail.com');
    INSERT INTO users(created_at,source,name,city,birth_date,latitude,zip,password,id,longitude,address,state,email) VALUES ('2018-05-24T06:18:20.069Z','Facebook','Roselyn Bosco','Leakesville','1996-01-19',31.2341055,39451,'43adf4af-055b-4a39-bc06-489fb0ffcf40',7,-88.5856948,'630 Coaker Road','MS','bosco.roselyn@hotmail.com');
    INSERT INTO users(created_at,source,name,city,birth_date,latitude,zip,password,id,longitude,address,state,email) VALUES ('2017-08-18T02:55:11.873Z','Facebook','Aracely Jenkins','Pittsburg','1973-06-05',37.43472089999999,66762,'6bb01b7f-6426-47d3-bfca-95dcc42b8b27',8,-94.6426865,'1167 East 570th Avenue','KS','aracely.jenkins@gmail.com');
    INSERT INTO users(created_at,source,name,city,birth_date,latitude,zip,password,id,longitude,address,state,email) VALUES ('2018-10-13T23:52:56.176Z','Twitter','Anais Ward','Ida Grove','1999-10-16',42.29790209999999,51445,'a3de5208-2f2f-4c81-9fe7-8d254bd5095d',9,-95.4673587,'5816-5894 280th Street','IA','ward.anais@gmail.com');
    INSERT INTO users(created_at,source,name,city,birth_date,latitude,zip,password,id,longitude,address,state,email) VALUES ('2018-08-16T12:16:30.307Z','Google','Tressa White','Upper Sandusky','1968-01-13',40.8006673,43351,'81052233-b32e-43cb-9505-700dbd8d3fca',10,-83.2838391,'13081-13217 Main Street','OH','white.tressa@yahoo.com');
    INSERT INTO users(created_at,source,name,city,birth_date,latitude,zip,password,id,longitude,address,state,email) VALUES ('2018-03-19T07:17:22.759Z','Facebook','Lolita Schaefer','Pilot Mound','1982-08-20',42.1394217,50223,'ceddd766-924b-4856-9dcb-a15b4c1b154c',11,-93.982366,'495 Juniper Road','IA','schaefer-lolita@hotmail.com');
    INSERT INTO users(created_at,source,name,city,birth_date,latitude,zip,password,id,longitude,address,state,email) VALUES ('2018-06-27T17:25:24.344Z','Facebook','Ciara Larson','Florence','1982-12-16',44.9564152,57235,'eb925d11-ea2f-41fb-a490-72b7c2b24dc0',12,-97.2287266,'16701-16743 449th Avenue','SD','ciara-larson@hotmail.com');
    INSERT INTO users(created_at,source,name,city,birth_date,latitude,zip,password,id,longitude,address,state,email) VALUES ('2017-09-21T16:21:06.408Z','Facebook','Mustafa Thiel','Santa Ysabel','1963-07-20',33.08172,92070,'bb85c7fa-4314-4e41-a060-7d0159f16931',13,-116.661853,'2993 Hoskings Ranch Road','CA','mustafa.thiel@hotmail.com');
    INSERT INTO users(created_at,source,name,city,birth_date,latitude,zip,password,id,longitude,address,state,email) VALUES ('2018-06-17T23:29:39.271Z','Facebook','Lavonne Senger','Chico','1963-09-22',39.6485802,95928,'8beebcc9-a376-447a-812f-7544fdc52ec7',14,-121.9343322,'3964 Chico River Road','CA','senger.lavonne@yahoo.com');
    INSERT INTO users(created_at,source,name,city,birth_date,latitude,zip,password,id,longitude,address,state,email) VALUES ('2018-12-20T17:50:32.296Z','Twitter','Bertrand Romaguera','El Paso','2000-02-14',35.1080351,72045,'2734ae7a-aa25-4907-9d2e-a6992750db60',15,-92.0101859,'258 Opal Road','AR','romaguera.bertrand@gmail.com');
    ```

1. Load some data into the orders table by running the following commands.

    ```sql
    yb_demo=# INSERT INTO orders(id,created_at,discount,product_id,quantity,subtotal,tax,total,user_id) VALUES (9,'2017-05-03T16:00:54.923Z',3.594742155259162,184,3,77.3982748679465,4.26,81.6742695904106,1);
    INSERT INTO orders(id,created_at,discount,product_id,quantity,subtotal,tax,total,user_id) VALUES (13,'2019-04-06T01:04:43.973Z',2.1173410336074987,70,2,57.493003808959784,3.95,61.42339339833593,3);
    INSERT INTO orders(id,created_at,discount,product_id,quantity,subtotal,tax,total,user_id) VALUES (21,'2018-05-02T03:57:22.388Z',NULL,94,5,109.21864156655383,7.51,116.62982729669602,3);
    INSERT INTO orders(id,created_at,discount,product_id,quantity,subtotal,tax,total,user_id) VALUES (22,'2019-12-12T21:32:01.533Z',6.752650070439861,10,1,47.6793282102869,1.38,49.056071014283766,4);
    INSERT INTO orders(id,created_at,discount,product_id,quantity,subtotal,tax,total,user_id) VALUES (23,'2019-06-02T11:33:15.096Z',NULL,85,5,54.90104734428525,1.59,56.5115886738793,4);
    INSERT INTO orders(id,created_at,discount,product_id,quantity,subtotal,tax,total,user_id) VALUES (26,'2018-06-20T02:18:00.254Z',NULL,40,5,99.66240044231697,3.99,103.57613671689575,5);
    INSERT INTO orders(id,created_at,discount,product_id,quantity,subtotal,tax,total,user_id) VALUES (27,'2019-04-28T07:01:15.932Z',NULL,14,2,37.648145389078365,1.51,39.11528698753412,6);
    INSERT INTO orders(id,created_at,discount,product_id,quantity,subtotal,tax,total,user_id) VALUES (42,'2019-11-16T08:11:08.666Z',NULL,145,3,61.1983004605443,4.28,65.5559297559252,7);
    INSERT INTO orders(id,created_at,discount,product_id,quantity,subtotal,tax,total,user_id) VALUES (43,'2019-11-03T20:24:38.219Z',NULL,193,3,50.38077396807232,3.27,53.70507001111754,8);
    INSERT INTO orders(id,created_at,discount,product_id,quantity,subtotal,tax,total,user_id) VALUES (44,'2017-10-01T14:47:29.444Z',NULL,101,6,93.21658710786936,6.06,99.33443001127412,8);
    INSERT INTO orders(id,created_at,discount,product_id,quantity,subtotal,tax,total,user_id) VALUES (53,'2018-09-15T13:06:14.139Z',NULL,79,3,41.616917284159726,2.5,44.22580561704055,9);
    INSERT INTO orders(id,created_at,discount,product_id,quantity,subtotal,tax,total,user_id) VALUES (54,'2018-10-26T20:41:23.428Z',7.63762060148704,68,4,115.24343882309758,6.63,122.11637851493803,10);
    INSERT INTO orders(id,created_at,discount,product_id,quantity,subtotal,tax,total,user_id) VALUES (74,'2020-02-20T00:36:30.807Z',1.8457579767720553,154,1,81.87529553312261,3.28,85.41374069109672,12);
    INSERT INTO orders(id,created_at,discount,product_id,quantity,subtotal,tax,total,user_id) VALUES (75,'2017-04-10T23:33:10.082Z',NULL,13,2,75.0861692740371,3,77.95866140384386,12);
    INSERT INTO orders(id,created_at,discount,product_id,quantity,subtotal,tax,total,user_id) VALUES (76,'2016-12-19T19:40:17.782Z',NULL,185,2,26.384667225677738,1.72,28.098902628941254,15);
    ```

1. Load some data into the reviews table by running the following commands.

    ```sql
    yb_demo=# INSERT INTO reviews(id,body,created_at,product_id,rating,reviewer) VALUES (1,'Ad perspiciatis quis et consectetur. Laboriosam fuga voluptas ut et modi ipsum. Odio et eum numquam eos nisi. Assumenda aut magnam libero maiores nobis vel beatae officia.','2018-05-15T20:25:48.517Z',1,5,'christ');
    INSERT INTO reviews(id,body,created_at,product_id,rating,reviewer) VALUES (2,'Reprehenderit non error architecto consequatur tempore temporibus. Voluptate ut accusantium quae est. Aut sit quidem nihil maxime dolores molestias. Enim vel optio est fugiat vitae cumque ut. Maiores laborum rerum quidem voluptate rerum.','2019-08-07T13:50:33.401Z',1,4,'xavier');
    INSERT INTO reviews(id,body,created_at,product_id,rating,reviewer) VALUES (3,'In aut numquam labore fuga. Et tempora sit et mollitia aut ullam et repellat. Aliquam sint tenetur culpa eius tenetur. Molestias ipsa est ut quisquam hic necessitatibus. Molestias maiores vero nesciunt.','2018-03-30T00:28:45.192Z',1,5,'cameron.nitzsche');
    INSERT INTO reviews(id,body,created_at,product_id,rating,reviewer) VALUES (4,'Est accusamus provident non animi labore minus aut mollitia. Officiis voluptatem quo dolorem sunt qui ipsum nobis totam. Et qui et qui quia ipsa ipsam minima.','2017-11-13T22:29:12.121Z',1,4,'barbara-shields');
    INSERT INTO reviews(id,body,created_at,product_id,rating,reviewer) VALUES (5,'Id sed sint corrupti molestias ad alias aut in. Nihil debitis ipsum repellendus voluptatem facere. Fugiat fugiat necessitatibus nobis hic.','2017-11-19T07:08:54.771Z',1,5,'clement');
    INSERT INTO reviews(id,body,created_at,product_id,rating,reviewer) VALUES (6,'Omnis pariatur autem adipisci eligendi. Eos aut accusantium dolorem et. Numquam vero debitis id provident odit doloremque enim.','2018-02-11T03:05:17.346Z',1,5,'jaunita');
    INSERT INTO reviews(id,body,created_at,product_id,rating,reviewer) VALUES (7,'Non unde voluptate nam quo. Quibusdam vero doloremque ut voluptas. Sequi commodi voluptatem vero debitis velit in. Quis dolores id qui aut voluptatibus. Magnam laborum sunt sit saepe reprehenderit.','2020-01-31T12:35:59.147Z',1,5,'perry.ruecker');
    INSERT INTO reviews(id,body,created_at,product_id,rating,reviewer) VALUES (8,'Quia ullam qui quae distinctio non nostrum laboriosam. Voluptatum velit et est dolore corporis sed. Dolore quia non illum quia omnis laudantium tempore.','2019-12-10T16:16:36.999Z',1,4,'cristina.balistreri');
    INSERT INTO reviews(id,body,created_at,product_id,rating,reviewer) VALUES (9,'Quo sed optio cum. Et officiis cumque quis. Facere unde porro sit voluptatem nulla incidunt. Rerum accusantium aut consequatur quae. Rerum ut eligendi vel repudiandae voluptates.','2019-11-06T10:43:14.868Z',3,4,'wilma-muller');
    INSERT INTO reviews(id,body,created_at,product_id,rating,reviewer) VALUES (10,'Est consectetur impedit sit. Distinctio corrupti ut magni provident recusandae aliquam qui error. Omnis et debitis pariatur doloribus quia blanditiis eaque. Voluptates ut eum minus quasi alias. Officiis nostrum facilis possimus.','2019-02-13T17:37:35.244Z',3,4,'herman-marquardt');
    INSERT INTO reviews(id,body,created_at,product_id,rating,reviewer) VALUES (11,'Blanditiis sequi reprehenderit nesciunt eos numquam a alias quibusdam. Et alias dolor vel. Non enim corporis magni dolorem voluptatem laudantium sit.','2018-10-22T23:12:30.534Z',3,4,'carolyne');
    INSERT INTO reviews(id,body,created_at,product_id,rating,reviewer) VALUES (12,'Sint sed et libero excepturi aut. Nihil tempora reprehenderit et et harum consectetur alias voluptatum. Sed et consequatur quibusdam natus nihil non illum.','2018-11-26T23:42:48.594Z',3,4,'ralph-klocko');
    INSERT INTO reviews(id,body,created_at,product_id,rating,reviewer) VALUES (13,'Enim consequatur voluptas temporibus iusto optio. Nihil et ea iste autem est. Accusamus sint corporis ullam.','2019-06-21T07:29:55.724Z',3,4,'gerry');
    INSERT INTO reviews(id,body,created_at,product_id,rating,reviewer) VALUES (14,'Placeat non inventore odit. Illum ullam rerum cum corrupti maiores. Nihil sequi molestias dolore explicabo doloremque nobis omnis. Saepe id voluptatem ut nemo. Commodi laborum qui amet hic rerum omnis iste.','2019-03-26T21:16:27.557Z',3,4,'lula-pouros');
    INSERT INTO reviews(id,body,created_at,product_id,rating,reviewer) VALUES (15,'Minus minima ea fugit vero consectetur. Voluptatibus dignissimos quibusdam alias quam eos deserunt maxime. Dolorem exercitationem ex nobis et esse odit accusamus voluptatum.','2019-11-30T07:00:39.059Z',3,4,'jalon.pagac');
    ```

    You now have sample data and are ready to begin exploring YSQL in YugabyteDB.

## Explore YugabyteDB

Display the schema of the `products` table as follows:

```sql
yb_demo=# \d products
```

```output
                                        Table "public.products"
   Column   |            Type             | Collation | Nullable |               Default                
------------+-----------------------------+-----------+----------+--------------------------------------
 id         | bigint                      |           | not null | nextval('products_id_seq'::regclass)
 created_at | timestamp without time zone |           |          | 
 category   | text                        |           |          | 
 ean        | text                        |           |          | 
 price      | double precision            |           |          | 
 quantity   | integer                     |           |          | 5000
 rating     | double precision            |           |          | 
 title      | text                        |           |          | 
 vendor     | text                        |           |          | 
Indexes:
    "products_pkey" PRIMARY KEY, lsm (id HASH)
```

### Simple queries

To see how many products there are in this table, run the following query.

```sql
yb_demo=# SELECT count(*) FROM products;
```

```output
 count
-------
   15
(1 row)
```

The following query selects the `id`, `title`, `category`, and `price` columns for the first five products.

```sql
yb_demo=# SELECT id, title, category, price, rating
          FROM products
          LIMIT 5;
```

```output
 id |           title           | category  |      price       | rating 
----+---------------------------+-----------+------------------+--------
  3 | Synergistic Granite Chair | Doohickey | 35.3887448815391 |      4
 14 | Awesome Concrete Shoes    | Widget    | 25.0987635927189 |      4
  9 | Practical Bronze Computer | Widget    | 58.3131209852614 |    4.2
 12 | Sleek Paper Toucan        | Gizmo     | 77.3428505441222 |    4.4
  5 | Enormous Marble Wallet    | Gadget    | 82.7450976850356 |      4
(5 rows)
```

### The JOIN clause

Use a JOIN clause to combine rows from two or more tables, based on a related column between them.

The following JOIN query selects the `total` column from the `orders` table, and for each of these orders, fetches the `id`, `name`, and `email` from the `users` table of the corresponding users that placed those orders. The related column between the two tables is the user's id.

```sql
yb_demo=# SELECT users.id, users.name, users.email, orders.id, orders.total
          FROM orders INNER JOIN users ON orders.user_id=users.id
          LIMIT 5;
```

```output
 id |     name           |         email                | id |      total       
----+--------------------+------------------------------+----+------------------
  4 | Arnold Adams       | adams.arnold@gmail.com       | 22 | 49.0560710142838
 15 | Bertrand Romaguera | romaguera.bertrand@gmail.com | 76 | 28.0989026289413
  1 | Hudson Borer       | borer-hudson@yahoo.com       |  9 | 81.6742695904106
 10 | Tressa White       | white.tressa@yahoo.com       | 54 | 122.116378514938
  4 | Arnold Adams       | adams.arnold@gmail.com       | 23 | 56.5115886738793
(5 rows)
```

### Distributed transactions

To track quantities accurately, each product being ordered in some quantity by a user has to decrement the corresponding product inventory quantity. These operations should be performed inside a transaction.

Imagine the user with id `1` wants to order `10` units of the product with id `2`.

Before running the transaction, you can verify the quantity of product `2` in stock by running the following query:

```sql
yb_demo=# SELECT id, category, price, quantity FROM products WHERE id=2;
```

```output
SELECT id, category, price, quantity FROM products WHERE id=2;
 id | category  |      price       | quantity
----+-----------+------------------+----------
  2 | Doohickey | 70.0798961307176 |     5000
(1 row)
```

To place the order, you can run the following transaction:

```sql
yb_demo=# BEGIN TRANSACTION;

/* First insert a new order into the orders table. */
INSERT INTO orders
  (id, created_at, user_id, product_id, discount, quantity, subtotal, tax, total)
VALUES (
  (SELECT max(id)+1 FROM orders)                 /* id */,
  now()                                          /* created_at */,
  1                                              /* user_id */,
  2                                              /* product_id */, 
  0                                              /* discount */,
  10                                             /* quantity */,
  (10 * (SELECT price FROM products WHERE id=2)) /* subtotal */,
  0                                              /* tax */,
  (10 * (SELECT price FROM products WHERE id=2)) /* total */
) RETURNING id;

/* Next decrement the total quantity from the products table. */
UPDATE products SET quantity = quantity - 10 WHERE id = 2;

COMMIT;
```

Verify that the order got inserted by running the following command:

```sql
yb_demo=# SELECT * FROM orders WHERE id = (SELECT max(id) FROM orders);
```

```output
 id |         created_at         | user_id | product_id | discount | quantity |     subtotal     | tax |      total       
----+----------------------------+---------+------------+----------+----------+------------------+-----+------------------
 77 | 2021-09-08 20:03:12.308302 |       1 |          2 |        0 |       10 | 700.798961307176 |   0 | 700.798961307176
(1 row)
```

To verify that total quantity of product id `2` in the inventory has been updated, run the following query:

```sql
yb_demo=# SELECT id, category, price, quantity FROM products WHERE id=2;
```

```output
 id | category  |      price       | quantity
----+-----------+------------------+----------
  2 | Doohickey | 70.0798961307176 |     4990
(1 row)
```

### Create a view

To answer questions such as what percentage of the total sales is from the Facebook channel, you can create a view.

```sql
yb_demo=# CREATE VIEW channel AS
            (SELECT source, ROUND(SUM(orders.total)) AS total_sales
             FROM users LEFT JOIN orders ON users.id=orders.user_id
             GROUP BY source
             ORDER BY total_sales DESC);
```

Now that the view is created, you can see it in the list of relations.

```sql
yb_demo=# \d
```

```output
               List of relations
 Schema |      Name       |   Type   |  Owner
--------+-----------------+----------+----------
 public | channel         | view     | yugabyte
 public | orders          | table    | yugabyte
 public | orders_id_seq   | sequence | yugabyte
 public | products        | table    | yugabyte
 public | products_id_seq | sequence | yugabyte
 public | reviews         | table    | yugabyte
 public | reviews_id_seq  | sequence | yugabyte
 public | users           | table    | yugabyte
 public | users_id_seq    | sequence | yugabyte
(9 rows)
```

```sql
yb_demo=# SELECT source, 
            total_sales * 100.0 / (SELECT SUM(total_sales) FROM channel) AS percent_sales
          FROM channel
          WHERE source='Facebook';
```

```output
  source  |  percent_sales   
----------+------------------
 Facebook | 31.3725490196078
(1 row)
```

### Learn more

For more information on the `ysqlsh` and `ycqlsh` shells, refer to [ysqlsh](../../admin/ysqlsh/) and [ycqlsh](../../admin/ycqlsh/).

For more information on the YSQL and YCQL APIs, refer to [YSQL API](../../api/ysql/) and [YCQL API](../../api/ycql/).

## Next steps

- Learn [cluster management basics](../cloud-basics), including how to

  - [Add paid clusters](../cloud-basics/create-clusters/)
  - [Assign IP allow lists](../cloud-basics/add-connections/)
  - [Connect to clusters](../cloud-basics/connect-to-clusters/)
  - [Add database users](../cloud-basics/add-users/)
