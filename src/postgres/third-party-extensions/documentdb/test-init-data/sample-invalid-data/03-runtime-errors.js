// Sample invalid JavaScript file for testing runtime errors
print("This file contains runtime errors");

use('test');

// Insert a valid document first
db.users.insertOne({
    _id: "user1",
    username: "alice_smith",
    email: "alice.smith@example.com",
    firstName: "Alice",
    lastName: "Smith",
    age: 28
});

print("First user inserted successfully");

// Cause a runtime error by accessing an undefined variable
print("Trying to access undefined variable: " + undefined_variable_name);

// This code will not be reached due to the runtime error above
db.users.insertOne({
    _id: "user2",
    username: "bob_jones",
    email: "bob.jones@example.com",
    firstName: "Bob",
    lastName: "Jones",
    age: 35
});

print("This line will never be reached due to the runtime error above");

// Try to call a method that doesn't exist
db.nonExistentCollection.nonExistentMethod();

print("This line also won't be reached");
