// Sample invalid JavaScript file for testing error handling
print("This file contains syntax errors and will cause initialization to fail");

use('test');

// Error 1: Missing closing quote
db.users.insertMany([
    {
        _id: "user1",
        username: "alice_smith",
        email: "alice.smith@example.com,  // Missing closing quote here
        firstName: "Alice",
        lastName: "Smith",
        age: 28
    }
]);

// Error 2: Missing closing bracket
db.products.insertMany([
    {
        _id: "product1",
        name: "Test Product",
        price: 29.99
    // Missing closing bracket here
]);

// Error 3: Invalid function call
this_function_does_not_exist();

// Error 4: Invalid field name
db.orders.insertOne({
    _id: "order1",
    $invalid_field: "This field name is invalid because it starts with $",
    customer: "user1"
});

print("This line will never be reached due to the errors above");
