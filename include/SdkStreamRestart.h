#pragma once

#include "as_camera_sdk_api.h"
#include <string>

struct SdkStreamRestartResult {
    bool success;
    bool stopped;
    std::string message;
};

inline SdkStreamRestartResult restartSdkStream(AS_CAM_PTR camera)
{
    int ret = AS_SDK_StopStream(camera);
    if (ret != 0) {
        return {false, false, "AS_SDK_StopStream failed: " + std::to_string(ret)};
    }

    // Use the same SDK mode as CameraSrv::onAttached. The publisher's stream_flg
    // contains bookkeeping bits (initially 0x0fffffff) which HP60C rejects with -85.
    // AS_SDK_GetStreamType is not implemented in the bundled SDK, so it cannot
    // supply a replacement mask. The stream controller will adjust subscriptions.
    ret = AS_SDK_StartStream(camera, DEFAULT_IMG_FLG);
    if (ret != 0) {
        return {false, true, "AS_SDK_StartStream failed: " + std::to_string(ret)};
    }
    return {true, true, ""};
}
