// Initialize analytics collection with sample data
print("Initializing analytics collection...");

// Switch to the sampledb database
use('sampledb');

// Create analytics collection and insert sample analytics data
db.analytics.insertMany([
    {
        _id: "analytics_2024_06",
        period: "2024-06",
        type: "monthly_summary",
        metrics: {
            totalUsers: 5,
            activeUsers: 4,
            newUsers: 2,
            totalOrders: 4,
            totalRevenue: 1721.08,
            averageOrderValue: 430.27,
            topSellingProducts: [
                { productId: "prod5", productName: "Professional Camera Lens", quantitySold: 1, revenue: 899.99 },
                { productId: "prod1", productName: "Wireless Bluetooth Headphones", quantitySold: 1, revenue: 199.99 },
                { productId: "prod3", productName: "Smart Fitness Watch", quantitySold: 1, revenue: 299.99 }
            ],
            categoryBreakdown: [
                { category: "Electronics", orders: 2, revenue: 499.98 },
                { category: "Photography", orders: 1, revenue: 899.99 },
                { category: "Food & Beverages", orders: 1, revenue: 74.97 },
                { category: "Lifestyle", orders: 3, revenue: 119.96 }
            ]
        },
        generatedAt: new Date("2024-07-01T00:00:00Z")
    },
    {
        _id: "user_activity_2024_06_20",
        date: "2024-06-20",
        type: "daily_user_activity",
        activities: [
            {
                userId: "user1",
                actions: [
                    { action: "login", timestamp: new Date("2024-06-20T08:30:00Z") },
                    { action: "view_product", productId: "prod2", timestamp: new Date("2024-06-20T08:35:00Z") },
                    { action: "view_orders", timestamp: new Date("2024-06-20T08:40:00Z") }
                ]
            },
            {
                userId: "user5",
                actions: [
                    { action: "login", timestamp: new Date("2024-06-20T14:15:00Z") },
                    { action: "view_product", productId: "prod5", timestamp: new Date("2024-06-20T14:20:00Z") },
                    { action: "add_to_cart", productId: "prod5", timestamp: new Date("2024-06-20T14:25:00Z") },
                    { action: "place_order", orderId: "order4", timestamp: new Date("2024-06-20T14:30:00Z") }
                ]
            }
        ]
    }
]);

print("Created " + db.analytics.countDocuments() + " analytics records");

// Create indexes for analytics queries
db.analytics.createIndex({ "period": 1 });
db.analytics.createIndex({ "type": 1 });
db.analytics.createIndex({ "date": 1 });

print("Created indexes on analytics collection");

// Create some aggregation views for common queries
print("Setting up sample aggregation examples...");

// Example aggregation: User order summary
print("Sample aggregation - User order summary:");
db.orders.aggregate([
    {
        $group: {
            _id: "$userId",
            totalOrders: { $sum: 1 },
            totalSpent: { $sum: "$orderSummary.total" },
            averageOrderValue: { $avg: "$orderSummary.total" }
        }
    },
    {
        $sort: { totalSpent: -1 }
    }
]).forEach(printjson);

print("Sample data initialization completed!");
