// Initialize orders collection with sample data
print("Initializing orders collection...");

// Switch to the test database
use('test');

// Create orders collection and insert sample orders
db.orders.insertMany([
    {
        _id: "order1",
        orderNumber: "ORD-2024-001",
        userId: "user1",
        customerInfo: {
            email: "alice.smith@example.com",
            firstName: "Alice",
            lastName: "Smith"
        },
        items: [
            {
                productId: "prod1",
                productName: "Wireless Bluetooth Headphones",
                quantity: 1,
                unitPrice: 199.99,
                totalPrice: 199.99
            },
            {
                productId: "prod4",
                productName: "Eco-Friendly Water Bottle",
                quantity: 2,
                unitPrice: 29.99,
                totalPrice: 59.98
            }
        ],
        subtotal: 259.97,
        tax: 20.80,
        shipping: 9.99,
        total: 290.76,
        currency: "USD",
        status: "completed",
        paymentMethod: "credit_card",
        shippingAddress: {
            street: "123 Main St",
            city: "Seattle",
            state: "WA",
            zipCode: "98101",
            country: "USA"
        },
        orderDate: new Date("2024-05-01T10:30:00Z"),
        shippedDate: new Date("2024-05-02T14:20:00Z"),
        deliveredDate: new Date("2024-05-05T16:45:00Z")
    },
    {
        _id: "order2",
        orderNumber: "ORD-2024-002",
        userId: "user2",
        customerInfo: {
            email: "bob.jones@example.com",
            firstName: "Bob",
            lastName: "Jones"
        },
        items: [
            {
                productId: "prod2",
                productName: "Organic Coffee Beans",
                quantity: 3,
                unitPrice: 24.99,
                totalPrice: 74.97
            }
        ],
        subtotal: 74.97,
        tax: 6.00,
        shipping: 5.99,
        total: 86.96,
        currency: "USD",
        status: "shipped",
        paymentMethod: "paypal",
        shippingAddress: {
            street: "456 Oak Ave",
            city: "San Francisco",
            state: "CA",
            zipCode: "94102",
            country: "USA"
        },
        orderDate: new Date("2024-05-15T09:15:00Z"),
        shippedDate: new Date("2024-05-16T11:30:00Z"),
        deliveredDate: null
    },
    {
        _id: "order3",
        orderNumber: "ORD-2024-003",
        userId: "user4",
        customerInfo: {
            email: "david.lee@example.com",
            firstName: "David",
            lastName: "Lee"
        },
        items: [
            {
                productId: "prod1",
                productName: "Wireless Bluetooth Headphones",
                quantity: 1,
                unitPrice: 199.99,
                totalPrice: 199.99
            },
            {
                productId: "prod2",
                productName: "Organic Coffee Beans",
                quantity: 1,
                unitPrice: 24.99,
                totalPrice: 24.99
            }
        ],
        subtotal: 224.98,
        tax: 18.00,
        shipping: 9.99,
        total: 252.97,
        currency: "USD",
        status: "processing",
        paymentMethod: "credit_card",
        shippingAddress: {
            street: "789 Pine St",
            city: "Austin",
            state: "TX",
            zipCode: "78701",
            country: "USA"
        },
        orderDate: new Date("2024-06-01T15:45:00Z"),
        shippedDate: null,
        deliveredDate: null
    },
    {
        _id: "order4",
        orderNumber: "ORD-2024-004",
        userId: "user1",
        customerInfo: {
            email: "alice.smith@example.com",
            firstName: "Alice",
            lastName: "Smith"
        },
        items: [
            {
                productId: "prod4",
                productName: "Eco-Friendly Water Bottle",
                quantity: 1,
                unitPrice: 29.99,
                totalPrice: 29.99
            }
        ],
        subtotal: 29.99,
        tax: 2.40,
        shipping: 4.99,
        total: 37.38,
        currency: "USD",
        status: "cancelled",
        paymentMethod: "credit_card",
        shippingAddress: {
            street: "123 Main St",
            city: "Seattle",
            state: "WA",
            zipCode: "98101",
            country: "USA"
        },
        orderDate: new Date("2024-06-10T12:20:00Z"),
        shippedDate: null,
        deliveredDate: null,
        cancelledDate: new Date("2024-06-11T08:30:00Z"),
        cancelReason: "Customer requested cancellation"
    }
]);

print("Created " + db.orders.countDocuments() + " orders in the orders collection");

// Create indexes for better query performance
db.orders.createIndex({ "userId": 1 });
db.orders.createIndex({ "status": 1 });
db.orders.createIndex({ "orderDate": 1 });
db.orders.createIndex({ "orderNumber": 1 }, { unique: true });
print("Created indexes on userId, status, orderDate, and orderNumber fields");

// Display summary statistics
print("\n=== Database Initialization Summary ===");
print("Users: " + db.users.countDocuments());
print("Products: " + db.products.countDocuments());
print("Orders: " + db.orders.countDocuments());
print("========================================");