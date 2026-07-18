# Sample Data for DocumentDB

This directory contains sample JavaScript files that initialize a DocumentDB instance with demo data. The data represents a simple e-commerce application with users, products, orders, and analytics.

## Collections Created

### 1. Users Collection (`01-users.js`)
Contains sample user accounts with the following structure:
- User profiles with personal information
- Preferences and settings
- Tags for categorization
- Indexed on email, username, city, and tags

### 2. Products Collection (`02-products.js`)
Contains sample product catalog with:
- Electronics, lifestyle, food, and photography items
- Detailed specifications and pricing
- Ratings and reviews data
- Indexed on category, brand, price, tags, and SKU

### 3. Orders Collection (`03-orders.js`)
Contains sample order data including:
- Order details and customer information
- Line items with product references
- Order status tracking
- Shipping and payment information
- Indexed on userId, orderNumber, status, orderDate, and customer email

### 4. Analytics Collection (`04-analytics.js`)
Contains sample analytics and reporting data:
- Monthly summary metrics
- Daily user activity logs
- Product performance data
- Sample aggregation examples

## Database Structure

All sample data is inserted into the `sampledb` database to keep it separate from any existing data.

## Usage

These files are automatically executed when the DocumentDB container starts (unless `--skip-init-data` or `SKIP_INIT_DATA=true` is supplied). They can also be run manually using mongosh:

```bash
mongosh localhost:10260 -u username -p mypassword --authenticationMechanism SCRAM-SHA-256 --tls --tlsAllowInvalidCertificates --file 01-users.js
```

## Data Overview

- **5 users** with diverse profiles and preferences
- **5 products** across different categories
- **4 orders** in various stages (pending, processing, shipped, delivered)
- **2 analytics records** with sample metrics and user activity

This sample data provides a foundation for testing queries, exploring DocumentDB features, and building applications.
