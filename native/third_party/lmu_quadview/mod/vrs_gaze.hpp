#pragma once

#include <openxr/openxr.h>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace lmuqv {

// Optional eye-gaze input, owned independently from the application's actions.
// Call load only when XR_EXT_eye_gaze_interaction was enabled on the instance.
// The owner serializes access and calls resetSession BEFORE destroying session
// or instance. No application action set, action, or space is ever destroyed.
class VrsGaze {
public:
    VrsGaze() = default;
    VrsGaze(const VrsGaze&) = delete;
    VrsGaze& operator=(const VrsGaze&) = delete;

    bool load(XrInstance instance, PFN_xrGetInstanceProcAddr getProc) {
        resetSession();
        instance_ = instance;
        loaded_ = false;
        bool ok = instance != XR_NULL_HANDLE && getProc != nullptr;
#define LMUQV_GAZE_LOAD(name) do { \
        name##_ = nullptr; \
        PFN_xrVoidFunction proc = nullptr; \
        if (!getProc || instance == XR_NULL_HANDLE || \
            XR_FAILED(getProc(instance, #name, &proc)) || !proc) ok = false; \
        else name##_ = reinterpret_cast<PFN_##name>(proc); \
    } while (false)
        LMUQV_GAZE_LOAD(xrCreateActionSet);
        LMUQV_GAZE_LOAD(xrCreateAction);
        LMUQV_GAZE_LOAD(xrStringToPath);
        LMUQV_GAZE_LOAD(xrSuggestInteractionProfileBindings);
        LMUQV_GAZE_LOAD(xrCreateActionSpace);
        LMUQV_GAZE_LOAD(xrDestroySpace);
        LMUQV_GAZE_LOAD(xrDestroyActionSet);
        LMUQV_GAZE_LOAD(xrAttachSessionActionSets);
        LMUQV_GAZE_LOAD(xrSyncActions);
        LMUQV_GAZE_LOAD(xrGetActionStatePose);
        LMUQV_GAZE_LOAD(xrLocateSpace);
#undef LMUQV_GAZE_LOAD
        loaded_ = ok;
        return loaded_;
    }

    bool available() const { return loaded_; }
    bool initialized() const { return space_ != XR_NULL_HANDLE; }

    bool initialize(XrSession session) {
        if (session == session_ && initialized()) return true;
        resetSession();
        if (!loaded_ || session == XR_NULL_HANDLE) return false;
        session_ = session;
        XrActionSetCreateInfo setInfo{}; setInfo.type = XR_TYPE_ACTION_SET_CREATE_INFO;
        std::strcpy(setInfo.actionSetName, "lmuqv_vrs_gaze");
        std::strcpy(setInfo.localizedActionSetName, "LMU VRS Gaze");
        if (XR_FAILED(xrCreateActionSet_(instance_, &setInfo, &set_))) return failInitialize();
        XrActionCreateInfo actionInfo{}; actionInfo.type = XR_TYPE_ACTION_CREATE_INFO;
        std::strcpy(actionInfo.actionName, "lmuqv_gaze_pose");
        std::strcpy(actionInfo.localizedActionName, "LMU Gaze Pose");
        actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
        if (XR_FAILED(xrCreateAction_(set_, &actionInfo, &action_))) return failInitialize();
        XrPath profile = XR_NULL_PATH, binding = XR_NULL_PATH;
        if (XR_FAILED(xrStringToPath_(instance_, "/interaction_profiles/ext/eye_gaze_interaction", &profile)) ||
            XR_FAILED(xrStringToPath_(instance_, "/user/eyes_ext/input/gaze_ext/pose", &binding)))
            return failInitialize();
        XrActionSuggestedBinding suggested{action_, binding};
        XrInteractionProfileSuggestedBinding bindings{}; bindings.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        bindings.interactionProfile = profile;
        bindings.countSuggestedBindings = 1;
        bindings.suggestedBindings = &suggested;
        if (XR_FAILED(xrSuggestInteractionProfileBindings_(instance_, &bindings))) return failInitialize();
        XrActionSpaceCreateInfo spaceInfo{}; spaceInfo.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
        spaceInfo.action = action_;
        spaceInfo.poseInActionSpace.orientation.w = 1;
        if (XR_FAILED(xrCreateActionSpace_(session_, &spaceInfo, &space_))) return failInitialize();
        return true;
    }

    XrResult attach(XrSession session, const XrSessionActionSetsAttachInfo* info) {
        if (!xrAttachSessionActionSets_) return XR_ERROR_FUNCTION_UNSUPPORTED;
        if (session != session_ || !initialized()) return xrAttachSessionActionSets_(session, info);
        appAttachObserved_ = true;
        // Leave invalid inputs untouched so the runtime reports the app's error.
        if (attached_ || !info || info->type != XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO ||
            !info->countActionSets || !info->actionSets ||
            info->countActionSets == std::numeric_limits<uint32_t>::max())
            return xrAttachSessionActionSets_(session, info);
        try {
            std::vector<XrActionSet> sets(info->actionSets, info->actionSets + info->countActionSets);
            sets.push_back(set_);
            auto augmented = *info;
            augmented.countActionSets = static_cast<uint32_t>(sets.size());
            augmented.actionSets = sets.data();
            const XrResult result = xrAttachSessionActionSets_(session, &augmented);
            if (XR_SUCCEEDED(result)) attached_ = true;
            return result;
        } catch (...) {
            // Optional gaze must not turn host allocation failure into an app failure.
            return xrAttachSessionActionSets_(session, info);
        }
    }

    XrResult sync(XrSession session, const XrActionsSyncInfo* info) {
        if (!xrSyncActions_) return XR_ERROR_FUNCTION_UNSUPPORTED;
        if (session != session_ || !initialized()) return xrSyncActions_(session, info);
        synced_ = false;
        if (!attached_ || !info || info->type != XR_TYPE_ACTIONS_SYNC_INFO ||
            (info->countActiveActionSets && !info->activeActionSets) ||
            info->countActiveActionSets == std::numeric_limits<uint32_t>::max())
            return xrSyncActions_(session, info);
        try {
            std::vector<XrActiveActionSet> sets;
            if (info->countActiveActionSets)
                sets.assign(info->activeActionSets, info->activeActionSets + info->countActiveActionSets);
            sets.push_back({set_, XR_NULL_PATH});
            auto augmented = *info;
            augmented.countActiveActionSets = static_cast<uint32_t>(sets.size());
            augmented.activeActionSets = sets.data();
            const XrResult result = xrSyncActions_(session, &augmented);
            // XR_SESSION_NOT_FOCUSED is a nonnegative result, but no valid gaze.
            synced_ = result == XR_SUCCESS;
            return result;
        } catch (...) {
            return xrSyncActions_(session, info);
        }
    }

    // Call after xrBeginSession, when the game reaches its first locate call.
    // A keyboard-only app may never attach or sync any actions. Attach privately
    // once in that case. OpenXR permits only one successful attachment: an app
    // that attaches later receives the runtime's ALREADY_ATTACHED error unchanged.
    // An app attachment attempt (even failed) suppresses this lazy attachment.
    bool ensureAttachedAndSync(XrSession session) {
        if (!initialized() || session != session_) return false;
        if (!attached_) {
            if (appAttachObserved_ || privateAttachAttempted_) return false;
            privateAttachAttempted_ = true;
            XrSessionActionSetsAttachInfo info{}; info.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
            info.countActionSets = 1;
            info.actionSets = &set_;
            if (XR_FAILED(xrAttachSessionActionSets_(session_, &info))) return false;
            attached_ = privateOnly_ = true;
        }
        if (!privateOnly_) return synced_;
        XrActiveActionSet active{set_, XR_NULL_PATH};
        XrActionsSyncInfo info{}; info.type = XR_TYPE_ACTIONS_SYNC_INFO;
        info.countActiveActionSets = 1;
        info.activeActionSets = &active;
        synced_ = xrSyncActions_(session_, &info) == XR_SUCCESS;
        return synced_;
    }

    // No pose cache: each success comes from a fresh locate at the supplied time
    // in the application's base space. Caller uses its fixed center on false.
    // After lazy attachment sample owns synchronization. With app action sets,
    // only the intercepted app sync is used, preserving all active game inputs.
    bool sample(XrSpace baseSpace, XrTime time, XrPosef& out) {
        if (!initialized() || !attached_ || baseSpace == XR_NULL_HANDLE) return false;
        if (privateOnly_ && !ensureAttachedAndSync(session_)) return false;
        if (!synced_) return false;
        XrActionStateGetInfo info{}; info.type = XR_TYPE_ACTION_STATE_GET_INFO;
        info.action = action_;
        XrActionStatePose state{}; state.type = XR_TYPE_ACTION_STATE_POSE;
        if (xrGetActionStatePose_(session_, &info, &state) != XR_SUCCESS || !state.isActive) return false;
        XrSpaceLocation location{}; location.type = XR_TYPE_SPACE_LOCATION;
        if (xrLocateSpace_(space_, baseSpace, time, &location) != XR_SUCCESS) return false;
        constexpr XrSpaceLocationFlags valid = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
        if ((location.locationFlags & valid) != valid || !validPose(location.pose)) return false;
        out = location.pose;
        return true;
    }

    void resetSession() {
        if (space_ && xrDestroySpace_) xrDestroySpace_(space_);
        if (set_ && xrDestroyActionSet_) xrDestroyActionSet_(set_); // Also destroys its action.
        session_ = XR_NULL_HANDLE;
        space_ = XR_NULL_HANDLE;
        set_ = XR_NULL_HANDLE;
        action_ = XR_NULL_HANDLE;
        attached_ = privateOnly_ = synced_ = appAttachObserved_ = privateAttachAttempted_ = false;
    }

private:
    bool failInitialize() { resetSession(); return false; }
    static bool validPose(const XrPosef& pose) {
        const auto& q = pose.orientation;
        const auto& p = pose.position;
        if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w) ||
            !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
        const float lengthSquared = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
        return std::isfinite(lengthSquared) && std::fabs(lengthSquared - 1.f) < 0.01f;
    }
    XrInstance instance_ = XR_NULL_HANDLE;
    XrSession session_ = XR_NULL_HANDLE;
    XrActionSet set_ = XR_NULL_HANDLE;
    XrAction action_ = XR_NULL_HANDLE;
    XrSpace space_ = XR_NULL_HANDLE;
    bool loaded_ = false, attached_ = false, privateOnly_ = false, synced_ = false;
    bool appAttachObserved_ = false, privateAttachAttempted_ = false;
#define LMUQV_GAZE_PROC(name) PFN_##name name##_ = nullptr
    LMUQV_GAZE_PROC(xrCreateActionSet);
    LMUQV_GAZE_PROC(xrCreateAction);
    LMUQV_GAZE_PROC(xrStringToPath);
    LMUQV_GAZE_PROC(xrSuggestInteractionProfileBindings);
    LMUQV_GAZE_PROC(xrCreateActionSpace);
    LMUQV_GAZE_PROC(xrDestroySpace);
    LMUQV_GAZE_PROC(xrDestroyActionSet);
    LMUQV_GAZE_PROC(xrAttachSessionActionSets);
    LMUQV_GAZE_PROC(xrSyncActions);
    LMUQV_GAZE_PROC(xrGetActionStatePose);
    LMUQV_GAZE_PROC(xrLocateSpace);
#undef LMUQV_GAZE_PROC
};

} // namespace lmuqv
