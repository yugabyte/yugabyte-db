#include <gtest/gtest.h>
#include <cpp_redis/builders/simple_string_builder.hpp>
#include <cpp_redis/builders/array_builder.hpp>
#include <cpp_redis/builders/integer_builder.hpp>
#include <cpp_redis/builders/bulk_string_builder.hpp>
#include <cpp_redis/builders/error_builder.hpp>
#include <cpp_redis/redis_error.hpp>

TEST(ArrayBuilder, WithNoData) {
    cpp_redis::builders::array_builder builder;

    EXPECT_EQ(false, builder.reply_ready());
}

TEST(ArrayBuilder, WithNotEnoughData) {
    cpp_redis::builders::array_builder builder;

    std::string buffer = "1\r\n";
    builder << buffer;

    EXPECT_EQ(false, builder.reply_ready());
}

TEST(ArrayBuilder, WithPartOfEndSequence) {
    cpp_redis::builders::array_builder builder;

    std::string buffer = "1\r\n+hello\r";
    builder << buffer;

    EXPECT_EQ(false, builder.reply_ready());
}

TEST(ArrayBuilder, WithAllInOneTime) {
    cpp_redis::builders::array_builder builder;

    std::string buffer = "4\r\n+simple_string\r\n-error\r\n:42\r\n$5\r\nhello\r\n";
    builder << buffer;

    EXPECT_EQ(true, builder.reply_ready());
    EXPECT_EQ("", buffer);

    auto reply = builder.get_reply();
    EXPECT_TRUE(reply.is_array());

    auto array = reply.as_array();
    EXPECT_EQ(4U, array.size());

    auto row_1 = array[0];
    EXPECT_TRUE(row_1.is_simple_string());
    EXPECT_EQ("simple_string", row_1.as_string());

    auto row_2 = array[1];
    EXPECT_TRUE(row_2.is_error());
    EXPECT_EQ("error", row_2.as_string());

    auto row_3 = array[2];
    EXPECT_TRUE(row_3.is_integer());
    EXPECT_EQ(42, row_3.as_integer());

    auto row_4 = array[3];
    EXPECT_TRUE(row_4.is_bulk_string());
    EXPECT_EQ("hello", row_4.as_string());
}

TEST(ArrayBuilder, WithAllInMultipleTimes) {
    cpp_redis::builders::array_builder builder;

    std::string buffer = "4\r\n+simple_string\r";
    builder << buffer;
    buffer += "\n-error\r\n:42\r\n";
    builder << buffer;
    buffer += "$5\r\nhello\r\n";
    builder << buffer;

    EXPECT_EQ(true, builder.reply_ready());
    EXPECT_EQ("", buffer);

    auto reply = builder.get_reply();
    EXPECT_TRUE(reply.is_array());

    auto array = reply.as_array();
    EXPECT_EQ(4U, array.size());

    auto row_1 = array[0];
    EXPECT_TRUE(row_1.is_simple_string());
    EXPECT_EQ("simple_string", row_1.as_string());

    auto row_2 = array[1];
    EXPECT_TRUE(row_2.is_error());
    EXPECT_EQ("error", row_2.as_string());

    auto row_3 = array[2];
    EXPECT_TRUE(row_3.is_integer());
    EXPECT_EQ(42, row_3.as_integer());

    auto row_4 = array[3];
    EXPECT_TRUE(row_4.is_bulk_string());
    EXPECT_EQ("hello", row_4.as_string());
}

TEST(ArrayBuilder, EmptyArray) {
    cpp_redis::builders::array_builder builder;

    std::string buffer = "0\r\n";
    builder << buffer;

    EXPECT_EQ(true, builder.reply_ready());
    EXPECT_EQ("", buffer);

    auto reply = builder.get_reply();
    EXPECT_TRUE(reply.is_array());

    auto array = reply.as_array();
    EXPECT_EQ(0U, array.size());
}


TEST(ArrayBuilder, InvalidSize) {
    cpp_redis::builders::array_builder builder;

    std::string buffer = "-1\r\n";
    EXPECT_THROW(builder << buffer, cpp_redis::redis_error);
}
