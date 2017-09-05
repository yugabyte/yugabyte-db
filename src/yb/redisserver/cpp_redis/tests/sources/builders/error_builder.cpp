#include <gtest/gtest.h>
#include <cpp_redis/builders/error_builder.hpp>
#include <cpp_redis/redis_error.hpp>

TEST(ErrorBuilder, WithNoData) {
    cpp_redis::builders::error_builder builder;

    EXPECT_EQ(false, builder.reply_ready());
}

TEST(ErrorBuilder, WithNotEnoughData) {
    cpp_redis::builders::error_builder builder;

    std::string buffer = "error";
    builder << buffer;

    EXPECT_EQ(false, builder.reply_ready());
}

TEST(ErrorBuilder, WithPartOfEndSequence) {
    cpp_redis::builders::error_builder builder;

    std::string buffer = "error\r";
    builder << buffer;

    EXPECT_EQ(false, builder.reply_ready());
}

TEST(ErrorBuilder, WithAllInOneTime) {
    cpp_redis::builders::error_builder builder;

    std::string buffer = "error\r\n";
    builder << buffer;

    EXPECT_EQ(true, builder.reply_ready());
    EXPECT_EQ("", buffer);

    auto reply = builder.get_reply();
    EXPECT_TRUE(reply.is_error());
    EXPECT_EQ("error", reply.as_string());
}

TEST(ErrorBuilder, WithAllInMultipleTimes) {
    cpp_redis::builders::error_builder builder;

    std::string buffer = "erro";
    builder << buffer;
    buffer += "r\r\n";
    builder << buffer;

    EXPECT_EQ(true, builder.reply_ready());
    EXPECT_EQ("", buffer);

    auto reply = builder.get_reply();
    EXPECT_TRUE(reply.is_error());
    EXPECT_EQ("error", reply.as_string());
}

TEST(ErrorBuilder, WithAllInMultipleTimes2) {
    cpp_redis::builders::error_builder builder;

    std::string buffer = "error";
    builder << buffer;
    buffer += "\r\n";
    builder << buffer;

    EXPECT_EQ(true, builder.reply_ready());
    EXPECT_EQ("", buffer);

    auto reply = builder.get_reply();
    EXPECT_TRUE(reply.is_error());
    EXPECT_EQ("error", reply.as_string());
}

TEST(ErrorBuilder, WithAllInMultipleTimes3) {
    cpp_redis::builders::error_builder builder;

    std::string buffer = "error\r";
    builder << buffer;
    buffer += "\n";
    builder << buffer;

    EXPECT_EQ(true, builder.reply_ready());
    EXPECT_EQ("", buffer);

    auto reply = builder.get_reply();
    EXPECT_TRUE(reply.is_error());
    EXPECT_EQ("error", reply.as_string());
}
