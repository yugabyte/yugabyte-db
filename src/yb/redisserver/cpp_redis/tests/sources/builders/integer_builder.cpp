#include <gtest/gtest.h>
#include <cpp_redis/builders/integer_builder.hpp>
#include <cpp_redis/redis_error.hpp>

TEST(IntegerBuilder, WithNoData) {
    cpp_redis::builders::integer_builder builder;

    EXPECT_EQ(false, builder.reply_ready());
}

TEST(IntegerBuilder, WithNotEnoughData) {
    cpp_redis::builders::integer_builder builder;

    std::string buffer = "42";
    builder << buffer;

    EXPECT_EQ(false, builder.reply_ready());
}

TEST(IntegerBuilder, WithPartOfEndSequence) {
    cpp_redis::builders::integer_builder builder;

    std::string buffer = "42\r";
    builder << buffer;

    EXPECT_EQ(false, builder.reply_ready());
}

TEST(IntegerBuilder, WithAllInOneTime) {
    cpp_redis::builders::integer_builder builder;

    std::string buffer = "42\r\n";
    builder << buffer;

    EXPECT_EQ(true, builder.reply_ready());
    EXPECT_EQ("", buffer);

    auto reply = builder.get_reply();
    EXPECT_TRUE(reply.is_integer());
    EXPECT_EQ(42, reply.as_integer());
}

TEST(IntegerBuilder, NegativeNumber) {
    cpp_redis::builders::integer_builder builder;

    std::string buffer = "-1\r\n";
    builder << buffer;

    EXPECT_EQ(true, builder.reply_ready());
    EXPECT_EQ("", buffer);

    auto reply = builder.get_reply();
    EXPECT_TRUE(reply.is_integer());
    EXPECT_EQ(-1, reply.as_integer());
}

TEST(IntegerBuilder, WithAllInMultipleTimes) {
    cpp_redis::builders::integer_builder builder;

    std::string buffer = "4";
    builder << buffer;
    buffer += "2\r\n";
    builder << buffer;

    EXPECT_EQ(true, builder.reply_ready());
    EXPECT_EQ("", buffer);

    auto reply = builder.get_reply();
    EXPECT_TRUE(reply.is_integer());
    EXPECT_EQ(42, reply.as_integer());
}

TEST(IntegerBuilder, WithAllInMultipleTimes2) {
    cpp_redis::builders::integer_builder builder;

    std::string buffer = "42";
    builder << buffer;
    buffer += "\r\n";
    builder << buffer;

    EXPECT_EQ(true, builder.reply_ready());
    EXPECT_EQ("", buffer);

    auto reply = builder.get_reply();
    EXPECT_TRUE(reply.is_integer());
    EXPECT_EQ(42, reply.as_integer());
}

TEST(IntegerBuilder, WithAllInMultipleTimes3) {
    cpp_redis::builders::integer_builder builder;

    std::string buffer = "42\r";
    builder << buffer;
    buffer += "\n";
    builder << buffer;

    EXPECT_EQ(true, builder.reply_ready());
    EXPECT_EQ("", buffer);

    auto reply = builder.get_reply();
    EXPECT_TRUE(reply.is_integer());
    EXPECT_EQ(42, reply.as_integer());
}

TEST(IntegerBuilder, WrongChar) {
    cpp_redis::builders::integer_builder builder;

    std::string buffer = "4a\r\n";
    EXPECT_THROW(builder << buffer, cpp_redis::redis_error);
}
