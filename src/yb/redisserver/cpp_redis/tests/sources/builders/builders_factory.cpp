#include <gtest/gtest.h>
#include <cpp_redis/builders/builders_factory.hpp>
#include <cpp_redis/redis_error.hpp>
#include <cpp_redis/builders/simple_string_builder.hpp>
#include <cpp_redis/builders/array_builder.hpp>
#include <cpp_redis/builders/integer_builder.hpp>
#include <cpp_redis/builders/bulk_string_builder.hpp>
#include <cpp_redis/builders/error_builder.hpp>

TEST(BuildersFactory, Array) {
    auto builder = cpp_redis::builders::create_builder('*');
    EXPECT_TRUE(dynamic_cast<cpp_redis::builders::array_builder*>(builder.get()) != nullptr);
}

TEST(BuildersFactory, BulkString) {
    auto builder = cpp_redis::builders::create_builder('$');
    EXPECT_TRUE(dynamic_cast<cpp_redis::builders::bulk_string_builder*>(builder.get()) != nullptr);
}

TEST(BuildersFactory, Error) {
    auto builder = cpp_redis::builders::create_builder('-');
    EXPECT_TRUE(dynamic_cast<cpp_redis::builders::error_builder*>(builder.get()) != nullptr);
}

TEST(BuildersFactory, Integer) {
    auto builder = cpp_redis::builders::create_builder(':');
    EXPECT_TRUE(dynamic_cast<cpp_redis::builders::integer_builder*>(builder.get()) != nullptr);
}

TEST(BuildersFactory, SimpleString) {
    auto builder = cpp_redis::builders::create_builder('+');
    EXPECT_TRUE(dynamic_cast<cpp_redis::builders::simple_string_builder*>(builder.get()) != nullptr);
}

TEST(BuildersFactory, Unknown) {
    EXPECT_THROW(cpp_redis::builders::create_builder('a'), cpp_redis::redis_error);
}
