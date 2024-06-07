---
title: Use an ORM
linkTitle: Use an ORM
description: Django ORM support for YugabyteDB
menu:
  v2.14:
    identifier: django-orm
    parent: python-drivers
    weight: 600
type: docs
---
<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../django/" class="nav-link active">
      <i class="fa-brands fa-java" aria-hidden="true"></i>
      Django ORM
    </a>
  </li>

  <li >
    <a href="../sqlalchemy/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      SQLAlchemy ORM
    </a>
  </li>

</ul>

[Django](https://www.djangoproject.com/) is a high-level Python web framework that encourages rapid development and clean, pragmatic design.

## CRUD operations

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps in the [Build an application](../../../quick-start/build-apps/python/ysql-django/) page under the Quick start section.

The following sections demonstrate how to perform common tasks required for Python application development using the Django ORM.

### Add the Dependencies

To download the Django Rest Framework to your project, run the following command:

```sh
pip3 install djangorestframework
```

Additionally, install the [YB backend for Django](https://github.com/yugabyte/yb-django) using the following command:

```sh
pip3 install django-yugabytedb
```

### Implement ORM mapping for YugabyteDB

1. After the dependencies are installed, start a Django project and create a new application using the following command:

   ```sh
   django-admin startproject yugabyteTest && cd yugabyteTest/
   ```

1. Set up a new Django application using the following command:

   ```sh
   python manage.py startapp testdb
   ```

1. After creating the application, configure it to connect to the database. To do this, change the application settings to provide the database credentials. For better performance with YugabyteDB, use persistent connections (set `CONN_MAX_AGE`).In the file `yugabyteTest/settings.py` add the following code:

   ```python
   DATABASES = {
       'default': {
           'ENGINE': 'django_yugabytedb',
           'NAME': 'yugabyte',
           'HOST': 'localhost',
           'PORT': 5433,
           'USER': 'yugabyte',
           'CONN_MAX_AGE': None,
           'PASSWORD': 'yugabyte'
       }
   }
   ```

1. You need the application and rest framework in the INSTALLED_APPS field. Replace the existing code with the following:

   ```python
   INSTALLED_APPS = [
       'rest_framework',
       'testdb.apps.TestdbConfig',
       'django.contrib.contenttypes',
       'django.contrib.auth',
   ]

   REST_FRAMEWORK = {
       'DEFAULT_AUTHENTICATION_CLASSES': [],
       'DEFAULT_PERMISSION_CLASSES': [],
       'UNAUTHENTICATED_USER': None,
   }
   ```

1. The next step is to create a model for the table. The table name is `users` and contains four columns - `user_id`,`firstName`,`lastName`, and `email`.

   Add the following code to `testdb/models.py`:

   ```python
   rom django.db import models

   class Users(models.Model):
       userId = models.AutoField(db_column='user_id', primary_key=True, serialize=False)
       firstName = models.CharField(max_length=50, db_column='first_name')
       lastName = models.CharField(max_length=50, db_column='last_name')
       email = models.CharField(max_length=100, db_column='user_email')

       class Meta:
           db_table = "users"

       def __str__(self):
           return '%d %s %s %s' % (self.userId, self.firstName, self.lastName, self.email)
   ```

1. After creating the model, you need to create a Serializer. Serializers allow complex data such as querysets and model instances to be converted to native Python datatypes, which can then be rendered into JSON, XML, or other content types. Serializers also provide deserialization, allowing parsed data to be converted back into complex types, after first validating the incoming data.

   Copy the following code into `testdb\serializers.py`:

   ```python
   from rest_framework import serializers, status
   from testdb.models import Users
   from django.core.exceptions import ValidationError

   class UserSerializer(serializers.ModelSerializer):
       class Meta:
           model = Users
           fields = ('userId', 'firstName', 'lastName', 'email')
   ```

1. To finish all the elements of the application, create a ViewSet. In `testdb/views.py`, add the following code:

   ```python
   from django.shortcuts import render
   from testdb.models import Users
   from rest_framework import viewsets
   from testdb.serializers import UserSerializer

   class UserViewSet(viewsets.ModelViewSet):
       queryset = Users.objects.all()
       serializer_class = UserSerializer
   ```

1. Finally map the URLs in `yugabyteTest/urls.py` by adding the following code:

   ```python
   from django.urls import include, re_path
   from rest_framework import routers
   from testdb.views import UserViewSet

   router = routers.SimpleRouter(trailing_slash=False)
   router.register(r'users', UserViewSet)
   urlpatterns = [
       re_path(r'^', include(router.urls))
   ]
   ```

   For Django versions earlier than 4.0, use the following code instead, as you can import the URLs using django.conf.urls:

   ```python
   from django.urls import path, include
   from django.conf.urls import url, include
   from rest_framework import routers
   from testdb.views import UserViewSet

   router = routers.SimpleRouter(trailing_slash=False)
   router.register(r'users', UserViewSet)

   urlpatterns = [
       url(r'^', include(router.urls))
   ]
   ```

1. This completes the configuration of your test application. The next steps are to create the migration files and apply the migrations to the database. To do this, run the following command:

   ```sh
   python3 manage.py makemigrations
   python3 manage.py migrate
   ```

   A users table should be created in your database. Use the [ysqlsh](../../../admin/ysqlsh/#starting-ysqlsh) client shell to verify that the users table has been created.

### Run the application

To run the application and insert a new row, execute the following steps.

1. Run the Django project using the following command:

    ```shell
    python3 manage.py runserver 8080
    ```

1. Insert a row using the following command:

    ```shell
    curl --data '{ "firstName" : "John", "lastName" : "Smith", "email" : "jsmith@yb.com" }' \
          -v -X POST -H 'Content-Type:application/json' http://localhost:8080/users
    ```

1. Verify that the new row is inserted using the following command:

    ```sh
    curl http://localhost:8080/users
    ```

    You should see the following output:

   ```output.json
   [{"userId":1,"firstName":"John","lastName":"Smith","email":"jsmith@yb.com"}]
   ```

   You can also verify this using the [ysqlsh](../../../admin/ysqlsh/#starting-ysqlsh) client shell.

## Next steps

- Explore [Scaling Python Applications](../../../explore/linear-scalability/) with YugabyteDB.
