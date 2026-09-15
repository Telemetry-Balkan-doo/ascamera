#include "CameraSrv.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

struct FakeCamera {
    AS_CAM_ATTR_S attrs{};
    AS_CAM_Stream_Cb_s callback{};
    bool destroyed = false;
    bool streaming = false;
};

AS_LISTENER_CALLBACK_S usb_listener{};
std::vector<std::unique_ptr<FakeCamera>> cameras;
std::vector<std::string> events;
int stop_error = 0;
int close_error = 0;

FakeCamera &cameraState(AS_CAM_PTR camera)
{
    auto &state = *static_cast<FakeCamera *>(camera);
    EXPECT_FALSE(state.destroyed) << "SDK received a destroyed camera handle";
    return state;
}

class CameraStatus : public ICameraStatus {
public:
    AS_CAM_PTR current = nullptr;
    std::vector<AS_CAM_PTR> frames;

    int onCameraAttached(AS_CAM_PTR camera, CamSvrStreamParam_s &, const AS_SDK_CAM_MODEL_E &) override
    {
        current = camera;
        events.push_back("status.attach");
        return 0;
    }

    int onCameraDetached(AS_CAM_PTR camera) override
    {
        EXPECT_EQ(current, camera);
        EXPECT_FALSE(cameraState(camera).destroyed);
        current = nullptr;
        events.push_back("status.detach");
        return 0;
    }

    int onCameraOpen(AS_CAM_PTR) override
    {
        events.push_back("status.open");
        return 0;
    }

    int onCameraClose(AS_CAM_PTR) override
    {
        events.push_back("status.close");
        return 0;
    }

    int onCameraStart(AS_CAM_PTR) override
    {
        events.push_back("status.start");
        return 0;
    }

    int onCameraStop(AS_CAM_PTR) override
    {
        events.push_back("status.stop");
        return 0;
    }

    void onCameraNewFrame(AS_CAM_PTR camera, const AS_SDK_Data_s *) override
    {
        frames.push_back(camera);
    }

    void onCameraNewMergeFrame(AS_CAM_PTR, const AS_SDK_MERGE_s *) override {}
};

class CameraHotplugTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        cameras.clear();
        events.clear();
        usb_listener = {};
        stop_error = 0;
        close_error = 0;

        char directory[] = "/tmp/ascamera-hotplug-XXXXXX";
        const auto path = mkdtemp(directory);
        ASSERT_NE(path, nullptr);
        config_directory = path;
        std::ofstream(config_directory / "hp60c_test.json") << "{}";

        server = std::make_unique<CameraSrv>(&status, config_directory.string());
        ASSERT_EQ(server->start(), 0);
        ASSERT_NE(usb_listener.onAttached, nullptr);
        ASSERT_NE(usb_listener.onDetached, nullptr);
    }

    void TearDown() override
    {
        stop_error = 0;
        close_error = 0;
        if (server) {
            server->stop();
            server.reset();
        }
        std::filesystem::remove_all(config_directory);
    }

    AS_CAM_ATTR_S usbAttrs(int device_number)
    {
        AS_CAM_ATTR_S attrs{};
        attrs.type = AS_CAMERA_ATTR_LNX_USB;
        attrs.attr.usbAttrs.bnum = 1;
        attrs.attr.usbAttrs.dnum = device_number;
        std::strcpy(attrs.attr.usbAttrs.port_numbers, "2-3");
        return attrs;
    }

    void attach(AS_CAM_ATTR_S &attrs)
    {
        usb_listener.onAttached(&attrs, usb_listener.privateData);
    }

    void detach(AS_CAM_ATTR_S &attrs)
    {
        usb_listener.onDetached(&attrs, usb_listener.privateData);
    }

    CameraStatus status;
    std::unique_ptr<CameraSrv> server;
    std::filesystem::path config_directory;
};

}  // namespace

// SDK doubles record the real CameraSrv callback lifecycle without ROS or USB.
std::string getSysTime() { return {}; }
int AS_SDK_Init() { return 0; }
int AS_SDK_Deinit() { return 0; }
int AS_SDK_StopListener() { return 0; }

int AS_SDK_GetSwVersion(char *version, unsigned int size)
{
    if (size != 0) {
        version[0] = '\0';
    }
    return 0;
}

int AS_SDK_StartListener(const AS_LISTENER_CALLBACK_S &callback, AS_LISTENER_TYPE_E type, bool)
{
    if (type == AS_LISTENNER_TYPE_USB) {
        usb_listener = callback;
    }
    return 0;
}

int AS_SDK_CreateCamHandle(AS_CAM_PTR &camera, AS_CAM_ATTR_S *attrs)
{
    auto state = std::make_unique<FakeCamera>();
    state->attrs = *attrs;
    camera = state.get();
    cameras.push_back(std::move(state));
    events.push_back("sdk.create");
    return 0;
}

int AS_SDK_DestoryCamHandle(AS_CAM_PTR camera)
{
    cameraState(camera).destroyed = true;
    events.push_back("sdk.destroy");
    return 0;
}

int AS_SDK_GetCameraAttrs(AS_CAM_PTR camera, AS_CAM_ATTR_S &attrs)
{
    attrs = cameraState(camera).attrs;
    return 0;
}

int AS_SDK_GetCameraModel(AS_CAM_PTR camera, AS_SDK_CAM_MODEL_E &model)
{
    cameraState(camera);
    model = AS_SDK_CAM_MODEL_HP60C;
    return 0;
}

int AS_SDK_OpenCamera(AS_CAM_PTR camera, const char *)
{
    cameraState(camera);
    events.push_back("sdk.open");
    return 0;
}

int AS_SDK_CloseCamera(AS_CAM_PTR camera)
{
    cameraState(camera);
    events.push_back("sdk.close");
    return close_error;
}

int AS_SDK_RegisterStreamCallback(AS_CAM_PTR camera, AS_CAM_Stream_Cb_s *callback)
{
    cameraState(camera).callback = *callback;
    return 0;
}

int AS_SDK_RegisterMergeFrameCallback(AS_CAM_PTR, AS_CAM_Merge_Cb_s *) { return 0; }

int AS_SDK_StartStream(AS_CAM_PTR camera, int type)
{
    EXPECT_EQ(type, DEFAULT_IMG_FLG);
    cameraState(camera).streaming = true;
    events.push_back("sdk.start");
    return 0;
}

int AS_SDK_StopStream(AS_CAM_PTR camera, int type)
{
    EXPECT_EQ(type, DEFAULT_IMG_FLG);
    cameraState(camera).streaming = false;
    events.push_back("sdk.stop");
    return stop_error;
}

TEST_F(CameraHotplugTest, DetachReleasesOldHandleAndReattachStreamsFromNewUsbDevice)
{
    auto old_attrs = usbAttrs(36);
    attach(old_attrs);
    ASSERT_EQ(cameras.size(), 1u);
    const auto old_camera = cameras[0].get();
    ASSERT_TRUE(old_camera->streaming);

    events.clear();
    detach(old_attrs);

    EXPECT_TRUE(old_camera->destroyed);
    EXPECT_EQ(status.current, nullptr);
    EXPECT_EQ(events, (std::vector<std::string>{
        "status.stop", "sdk.stop", "status.close", "sdk.close", "status.detach", "sdk.destroy"}));

    auto new_attrs = usbAttrs(38);
    attach(new_attrs);
    ASSERT_EQ(cameras.size(), 2u);
    const auto new_camera = cameras[1].get();
    EXPECT_NE(new_camera, old_camera);
    EXPECT_EQ(status.current, new_camera);
    ASSERT_TRUE(new_camera->streaming);
    ASSERT_NE(new_camera->callback.callback, nullptr);

    AS_SDK_Data_s frame{};
    new_camera->callback.callback(new_camera, &frame, new_camera->callback.privateData);
    EXPECT_EQ(status.frames, (std::vector<AS_CAM_PTR>{new_camera}));
}

TEST_F(CameraHotplugTest, UsbRemovalErrorsDoNotKeepOldDeviceInList)
{
    auto old_attrs = usbAttrs(36);
    attach(old_attrs);
    ASSERT_EQ(cameras.size(), 1u);
    stop_error = -4;
    close_error = -4;

    detach(old_attrs);
    EXPECT_TRUE(cameras[0]->destroyed);
    EXPECT_EQ(status.current, nullptr);

    auto new_attrs = usbAttrs(38);
    attach(new_attrs);
    ASSERT_EQ(cameras.size(), 2u);
    EXPECT_TRUE(cameras[1]->streaming);
    EXPECT_EQ(status.current, cameras[1].get());
}

TEST_F(CameraHotplugTest, LateDetachForOldUsbAddressDoesNotCloseNewDevice)
{
    auto old_attrs = usbAttrs(36);
    attach(old_attrs);
    detach(old_attrs);
    auto new_attrs = usbAttrs(38);
    attach(new_attrs);
    ASSERT_EQ(cameras.size(), 2u);

    events.clear();
    detach(old_attrs);

    EXPECT_TRUE(events.empty());
    EXPECT_FALSE(cameras[1]->destroyed);
    EXPECT_TRUE(cameras[1]->streaming);
    EXPECT_EQ(status.current, cameras[1].get());
}

TEST_F(CameraHotplugTest, DuplicateAttachKeepsExistingDevice)
{
    auto attrs = usbAttrs(36);
    attach(attrs);
    ASSERT_EQ(cameras.size(), 1u);
    const auto camera = cameras[0].get();

    events.clear();
    attach(attrs);

    EXPECT_TRUE(events.empty());
    EXPECT_EQ(cameras.size(), 1u);
    EXPECT_EQ(status.current, camera);
    EXPECT_TRUE(camera->streaming);
}
