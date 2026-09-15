#include "SdkStreamRestart.h"

#include <gtest/gtest.h>
#include <vector>

namespace {

struct StreamCall {
    bool start;
    AS_CAM_PTR camera;
    int type;
};

std::vector<StreamCall> stream_calls;
int stop_result;
int start_result;

class SdkStreamRestartTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        stream_calls.clear();
        stop_result = 0;
        start_result = 0;
    }

    int camera_storage = 0;
    AS_CAM_PTR camera = &camera_storage;
};

}  // namespace

// Fake SDK entry points: the tests exercise restart ordering and failure handling
// without linking the vendor library or requiring a connected camera.
int AS_SDK_StopStream(AS_CAM_PTR camera, int type)
{
    stream_calls.push_back({false, camera, type});
    return stop_result;
}

int AS_SDK_StartStream(AS_CAM_PTR camera, int type)
{
    stream_calls.push_back({true, camera, type});
    return start_result;
}

TEST_F(SdkStreamRestartTest, RestartsWithSdkDefaultModeAfterStopping)
{
    const auto result = restartSdkStream(camera);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.stopped);
    EXPECT_TRUE(result.message.empty());
    ASSERT_EQ(stream_calls.size(), 2u);
    EXPECT_FALSE(stream_calls[0].start);
    EXPECT_EQ(stream_calls[0].camera, camera);
    EXPECT_EQ(stream_calls[0].type, 0);
    EXPECT_TRUE(stream_calls[1].start);
    EXPECT_EQ(stream_calls[1].camera, camera);
    EXPECT_EQ(stream_calls[1].type, 0);
}

TEST_F(SdkStreamRestartTest, DoesNotStartWhenStoppingFails)
{
    stop_result = -7;

    const auto result = restartSdkStream(camera);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.stopped);
    EXPECT_EQ(result.message, "AS_SDK_StopStream failed: -7");
    ASSERT_EQ(stream_calls.size(), 1u);
    EXPECT_FALSE(stream_calls[0].start);
    EXPECT_EQ(stream_calls[0].camera, camera);
}

TEST_F(SdkStreamRestartTest, ReportsStoppedStateWhenRestartFails)
{
    start_result = -85;

    const auto result = restartSdkStream(camera);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.stopped);
    EXPECT_EQ(result.message, "AS_SDK_StartStream failed: -85");
    ASSERT_EQ(stream_calls.size(), 2u);
    EXPECT_FALSE(stream_calls[0].start);
    EXPECT_TRUE(stream_calls[1].start);
}

TEST_F(SdkStreamRestartTest, TreatsPositiveSdkErrorAsFailure)
{
    stop_result = 1;

    const auto result = restartSdkStream(camera);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.stopped);
    EXPECT_EQ(result.message, "AS_SDK_StopStream failed: 1");
    ASSERT_EQ(stream_calls.size(), 1u);
    EXPECT_FALSE(stream_calls[0].start);
}
