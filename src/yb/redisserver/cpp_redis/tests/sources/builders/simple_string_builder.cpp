#include <gtest/gtest.h>
#include <cpp_redis/builders/simple_string_builder.hpp>
#include <cpp_redis/redis_error.hpp>

TEST(SimpleStringBuilder, WithNoData) {
    cpp_redis::builders::simple_string_builder builder;

    EXPECT_EQ(false, builder.reply_ready());
}

TEST(SimpleStringBuilder, WithNotEnoughData) {
    cpp_redis::builders::simple_string_builder builder;

    std::string buffer = "simple_string";
    builder << buffer;

    EXPECT_EQ(false, builder.reply_ready());
}

TEST(SimpleStringBuilder, WithPartOfEndSequence) {
    cpp_redis::builders::simple_string_builder builder;

    std::string buffer = "simple_string\r";
    builder << buffer;

    EXPECT_EQ(false, builder.reply_ready());
}

TEST(SimpleStringBuilder, WithAllInOneTime) {
    cpp_redis::builders::simple_string_builder builder;

    std::string buffer = "simple_string\r\n";
    builder << buffer;

    EXPECT_EQ(true, builder.reply_ready());
    EXPECT_EQ("", buffer);

    auto reply = builder.get_reply();
    EXPECT_TRUE(reply.is_simple_string());
    EXPECT_EQ("simple_string", reply.as_string());
}

TEST(SimpleStringBuilder, WithAllInMultipleTimes) {
    cpp_redis::builders::simple_string_builder builder;

    std::string buffer = "simple_";
    builder << buffer;
    buffer += "string\r\n";
    builder << buffer;

    EXPECT_EQ(true, builder.reply_ready());
    EXPECT_EQ("", buffer);

    auto reply = builder.get_reply();
    EXPECT_TRUE(reply.is_simple_string());
    EXPECT_EQ("simple_string", reply.as_string());
}

TEST(SimpleStringBuilder, WithAllInMultipleTimes2) {
    cpp_redis::builders::simple_string_builder builder;

    std::string buffer = "simple_string";
    builder << buffer;
    buffer += "\r\n";
    builder << buffer;

    EXPECT_EQ(true, builder.reply_ready());
    EXPECT_EQ("", buffer);

    auto reply = builder.get_reply();
    EXPECT_TRUE(reply.is_simple_string());
    EXPECT_EQ("simple_string", reply.as_string());
}

TEST(SimpleStringBuilder, WithAllInMultipleTimes3) {
    cpp_redis::builders::simple_string_builder builder;

    std::string buffer = "simple_string\r";
    builder << buffer;
    buffer += "\n";
    builder << buffer;

    EXPECT_EQ(true, builder.reply_ready());
    EXPECT_EQ("", buffer);

    auto reply = builder.get_reply();
    EXPECT_TRUE(reply.is_simple_string());
    EXPECT_EQ("simple_string", reply.as_string());
}
