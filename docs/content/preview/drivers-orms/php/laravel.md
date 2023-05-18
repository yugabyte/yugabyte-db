---
title: Laravel ORM
headerTitle: Use an ORM
linkTitle: Use an ORM
description: Laravel ORM support for YugabyteDB
headcontent: Laravel ORM support for YugabyteDB
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    identifier: laravel-orm
    parent: php-drivers
    weight: 500
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../laravel/" class="nav-link active">
      Laravel ORM
    </a>
  </li>
</ul>

[Laravel](https://laravel.com/docs/10.x/readme) is a PHP web application framework. Along with many features required for web development, Laravel includes [Eloquent](https://laravel.com/docs/10.x/eloquent#introduction) ORM which provides a robust database ORM.

YugabyteDB YSQL API has full compatibility with Laravel's Eloquent ORM for data persistence in PHP applications.

Learn the basic steps required for connecting to the YugabyteDB database using Laravel framework. The full working application can be found on the [PHP ORM example application](../../orms/php/ysql-laravel/) page.

### Step 1: Create a Laravel project

```txt
composer create-project --prefer-dist laravel/laravel yb-laravel-example
```

Note: Review the [prerequisites](../../orms/php/ysql-laravel/#prerequisites) for working with a Laravel project.

### Step 2: Configure the datasource in Laravel project

Update the `.env` file in the `yb-laravel-example` directory to configure the Laravel project to connect to the YugabyteDB cluster as follows:

```txt
DB_CONNECTION=pgsql
DB_HOST=127.0.0.1
DB_PORT=5433
DB_DATABASE=yugabyte
DB_USERNAME=yugabyte
DB_PASSWORD=
```

Given YSQL's compatibility with the PostgreSQL language, the `DB_CONNECTION`  property is set to `pgsql`.

Note: This step assumes that YugabyteDB database is running at `127.0.0.1:5433`.

### Step 3: Generate the Employees Model class

Eloquent ORM works with database tables using the [Model](https://laravel.com/docs/10.x/eloquent#generating-model-classes) classes.

In the `yb-laravel-example` directory, create a new model class `Employees` using the following command:

```php
php artisan make:model Employees  --migration
```

This command generates model classes in the `app\Models` directory and migration classes in the `database\migrations` directory respectively.

Update the `database\migrations\xxxx_create_employees_table.php` file to define the `employees` table.

```php
    public function up()
    {
        Schema::create('employees', function (Blueprint $table) {
            $table->id();
            $table->string('name');
            $table->integer('age');
            $table->string('email');
        });
    }
```

Update the `app\Models\Employees.php` file to map the columns of the `Employees` table.

```php
class Employees extends Model
{
    public $timestamps = false;
    use HasFactory;

    protected $fillable = [
        'name',
        'age',
        'email',
    ];
}
```

### Step 4: Create a seeder for Employees table

Laravel provides the ability to load a table with data using [seed](https://laravel.com/docs/10.x/seeding) and [Eloquent Factories](https://laravel.com/docs/10.x/eloquent-factories). Create the files required for seeding the `Employees` model as follows:

```txt
php artisan make:factory EmployeesFactory
php artisan make:seeder EmployeesSeeder
```

The `EmployeesFactory.php` file is generated in the `database\factories` directory and the `EmployeesSeeder.php` file is generated in the `database\seeders` directory.

Add the following code to load 10 dummy employees into the `Employees` table:

```php
class EmployeesFactory extends Factory
{
    /**
     * Define the model's default state.
     *
     * @return array
     */
    public function definition()
    {
        return [
            'name' => $this->faker->name(),
            'age' => $this->faker->randomNumber(2),
            'email' => $this->faker->unique()->safeEmail,
        ];
    }
}

```

```php
class EmployeesSeeder extends Seeder
{
    /**
     * Run the database seeds.
     *
     * @return void
     */
    public function run()
    {
        \App\Models\Employees::factory(10)->create();
    }
}
```

### Step 5: Apply the migrations and seed the table

Laravel provides a way to apply the DDLs against the database using [Migrations](https://laravel.com/docs/10.x/migrations). The database migrations are generated in the `database\migrations` directory. 

Run the following command to create and seed the `Employees` table in YugabyteDB:

```sh
php artisan migrate:fresh
php artisan db:seed --class=EmployeesSeeder
```

### Step 6: Create a controller for enabling CRUD for Employees model

Laravel supports automatically generating the controller required for a given model.

```sh
php artisan make:resource EmployeesResource
php artisan make:resource EmployeesCollection
php artisan make:controller EmployeesController
```

`EmployeesController.php` is generated in the `app/Http/Controllers` directory. Add the following content to the controller file:

```php
<?php

namespace App\Http\Controllers;

use App\Models\Employees;
use Illuminate\Http\Request;

class EmployeesController extends Controller
{
    public function index()
    {
        return Employees::all();
    }
 
    public function show($id)
    {
        return Employees::find($id);
    }

    public function store(Request $request)
    {
        return Employees::create($request->all());
    }

    public function update(Request $request, $id)
    {
        $article = Employees::findOrFail($id);
        $article->update($request->all());

        return $article;
    }

    public function delete(Request $request, $id)
    {
        $article = Employees::findOrFail($id);
        $article->delete();

        return 204;
    }
}
```

Add the following code to the `routes\api.php` file to enable HTTP access for performing CRUD against the `Employees` table:

```php
Route::get('/employees', function () {
    return EmployeesResource::collection(Employees::all());
});

Route::post('/employees', [EmployeesController::class, 'store']);
```

## Run the project

Laravel provides an embedded web server to handle the HTTP request. Start the application using the following command:

```txt
php artisan serve
```

Create an employee using a POST request.

```txt
curl -X POST http://localhost:8000/api/employees \
-H "Content-Type: application/x-www-form-urlencoded" \
-d "name=yugadev&age=25&email=yugadev@yugabyte.com"
```

Check the database for the new employee:

```sql
yugabyte=# select * from employees where name='yugadev';
 id  |  name   | age |        email
-----+---------+-----+----------------------
 201 | yugadev |  25 | yugadev@yugabyte.com
(1 row)
```

Get the details of all the `Employees` using a GET request.

```txt
curl  -v -X GET http://localhost:8000/api/employees
```