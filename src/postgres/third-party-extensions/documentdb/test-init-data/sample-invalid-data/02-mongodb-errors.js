// Sample invalid JavaScript file for testing MongoDB operation errors
print("This file contains MongoDB operation errors");

use('test');

// First, insert a valid document
db.users.insertOne({
    _id: "user1",
    username: "alice_smith",
    email: "alice.smith@example.com",
    firstName: "Alice",
    lastName: "Smith",
    age: 28
});

print("First user inserted successfully");

// Now try to insert a duplicate _id - this should fail
db.users.insertOne({
    _id: "user1",  // This will cause a duplicate key error
    username: "duplicate_user",
    email: "duplicate@example.com",
    firstName: "Duplicate",
    lastName: "User",
    age: 30
});

print("This line should not be reached due to the duplicate key error above");

// Additional operations that won't be executed due to the error
db.products.insertMany([
    {
        _id: "product1",
        name: "Test Product",
        price: 29.99
    },
    {
        _id: "product2", 
        name: "Another Product",
        price: 39.99
    }
]);

print("Product insertion completed - this won't be printed");
