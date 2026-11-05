#include "../vrs_gaze.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
template<class T> T handle(unsigned n) { return reinterpret_cast<T>(static_cast<uintptr_t>(n)); }
const auto instance = handle<XrInstance>(1);
const auto session = handle<XrSession>(2);
const auto privateSet = handle<XrActionSet>(3);
const auto action = handle<XrAction>(4);
const auto actionSpace = handle<XrSpace>(5);
const auto baseSpace = handle<XrSpace>(7);
const auto appSet = handle<XrActionSet>(8);
struct Runtime {
    XrResult attachResult = XR_SUCCESS, syncResult = XR_SUCCESS;
    XrResult poseResult = XR_SUCCESS, locateResult = XR_SUCCESS;
    XrResult createResult = XR_SUCCESS, bindingResult = XR_SUCCESS;
    XrResult spaceResult = XR_SUCCESS;
    bool active = true, attached = false;
    XrSpaceLocationFlags flags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    XrPosef pose{{0, 0, 0, 1}, {1, 2, 3}};
    int attaches = 0, syncs = 0, gets = 0, locates = 0, creates = 0;
    const void* attachNext = nullptr;
    const void* syncNext = nullptr;
    const void* attachPointer = nullptr;
    const void* syncPointer = nullptr;
    XrTime locatedTime = 0;
    XrSpace locatedBase = XR_NULL_HANDLE;
    std::vector<XrActionSet> sets;
    std::vector<XrActiveActionSet> activeSets;
    std::vector<int> destroyed;
    std::vector<std::string> paths;
    std::string missing;
} rt;
XrResult XRAPI_CALL createSet(XrInstance i, const XrActionSetCreateInfo* info, XrActionSet* out) {
    assert(i == instance && info->type == XR_TYPE_ACTION_SET_CREATE_INFO);
    ++rt.creates; if (XR_FAILED(rt.createResult)) return rt.createResult;
    *out = privateSet; return XR_SUCCESS;
}
XrResult XRAPI_CALL createAction(XrActionSet s, const XrActionCreateInfo* info, XrAction* out) {
    assert(s == privateSet && info->actionType == XR_ACTION_TYPE_POSE_INPUT);
    assert(info->countSubactionPaths == 0); *out = action; return XR_SUCCESS;
}
XrResult XRAPI_CALL stringPath(XrInstance i, const char* str, XrPath* out) {
    assert(i == instance); rt.paths.emplace_back(str); *out = rt.paths.size(); return XR_SUCCESS;
}
XrResult XRAPI_CALL suggest(XrInstance i, const XrInteractionProfileSuggestedBinding* info) {
    assert(i == instance && info->countSuggestedBindings == 1);
    assert(info->suggestedBindings[0].action == action);
    assert(rt.paths.at(info->interactionProfile - 1) == "/interaction_profiles/ext/eye_gaze_interaction");
    assert(rt.paths.at(info->suggestedBindings[0].binding - 1) == "/user/eyes_ext/input/gaze_ext/pose");
    return rt.bindingResult;
}
XrResult XRAPI_CALL createActionSpace(XrSession s, const XrActionSpaceCreateInfo* info, XrSpace* out) {
    assert(s == session && info->action == action && info->poseInActionSpace.orientation.w == 1);
    if (XR_FAILED(rt.spaceResult)) return rt.spaceResult;
    *out = actionSpace; return XR_SUCCESS;
}
XrResult XRAPI_CALL destroySpace(XrSpace s) {
    assert(s == actionSpace); rt.destroyed.push_back(5); return XR_SUCCESS;
}
XrResult XRAPI_CALL destroySet(XrActionSet s) {
    assert(s == privateSet); rt.destroyed.push_back(3); return XR_SUCCESS;
}
XrResult XRAPI_CALL attach(XrSession s, const XrSessionActionSetsAttachInfo* info) {
    ++rt.attaches; rt.attachPointer = info;
    if (s != session) return XR_ERROR_HANDLE_INVALID;
    if (!info || info->type != XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO || !info->countActionSets || !info->actionSets)
        return XR_ERROR_VALIDATION_FAILURE;
    rt.attachNext = info->next;
    rt.sets.assign(info->actionSets, info->actionSets + info->countActionSets);
    if (rt.attached) return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
    if (XR_SUCCEEDED(rt.attachResult)) rt.attached = true;
    return rt.attachResult;
}
XrResult XRAPI_CALL sync(XrSession s, const XrActionsSyncInfo* info) {
    ++rt.syncs; rt.syncPointer = info;
    if (s != session) return XR_ERROR_HANDLE_INVALID;
    if (!info || info->type != XR_TYPE_ACTIONS_SYNC_INFO || (info->countActiveActionSets && !info->activeActionSets))
        return XR_ERROR_VALIDATION_FAILURE;
    rt.syncNext = info->next;
    rt.activeSets.clear();
    for (uint32_t n = 0; n < info->countActiveActionSets; ++n) rt.activeSets.push_back(info->activeActionSets[n]);
    return rt.syncResult;
}
XrResult XRAPI_CALL getPose(XrSession s, const XrActionStateGetInfo* info, XrActionStatePose* out) {
    assert(s == session && info->action == action); ++rt.gets;
    out->isActive = rt.active ? XR_TRUE : XR_FALSE; return rt.poseResult;
}
XrResult XRAPI_CALL locate(XrSpace s, XrSpace b, XrTime t, XrSpaceLocation* out) {
    assert(s == actionSpace); ++rt.locates; rt.locatedBase = b; rt.locatedTime = t;
    out->locationFlags = rt.flags; out->pose = rt.pose; return rt.locateResult;
}
XrResult XRAPI_CALL getProc(XrInstance i, const char* name, PFN_xrVoidFunction* out) {
    assert(i == instance); *out = nullptr;
    if (rt.missing == name) return XR_ERROR_FUNCTION_UNSUPPORTED;
#define PROC(n, f) if (std::strcmp(name, n) == 0) { *out = reinterpret_cast<PFN_xrVoidFunction>(f); return XR_SUCCESS; }
    PROC("xrCreateActionSet", createSet) PROC("xrCreateAction", createAction)
    PROC("xrStringToPath", stringPath) PROC("xrSuggestInteractionProfileBindings", suggest)
    PROC("xrCreateActionSpace", createActionSpace)
    PROC("xrDestroySpace", destroySpace) PROC("xrDestroyActionSet", destroySet)
    PROC("xrAttachSessionActionSets", attach) PROC("xrSyncActions", sync)
    PROC("xrGetActionStatePose", getPose) PROC("xrLocateSpace", locate)
#undef PROC
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}
void initialize(lmuqv::VrsGaze& gaze) { assert(gaze.load(instance, getProc)); assert(gaze.initialize(session)); }
void appAttachAndSync(lmuqv::VrsGaze& gaze) {
    XrSessionActionSetsAttachInfo ai{}; ai.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
    ai.countActionSets = 1; ai.actionSets = &appSet;
    assert(gaze.attach(session, &ai) == XR_SUCCESS);
    XrActiveActionSet active{appSet, 123};
    XrActionsSyncInfo si{}; si.type = XR_TYPE_ACTIONS_SYNC_INFO; si.countActiveActionSets = 1; si.activeActionSets = &active;
    assert(gaze.sync(session, &si) == XR_SUCCESS);
}
void append_preserves_application_input() {
    rt = {}; lmuqv::VrsGaze gaze; initialize(gaze);
    int marker = 42;
    XrSessionActionSetsAttachInfo ai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO, &marker, 1, &appSet};
    assert(gaze.attach(session, &ai) == XR_SUCCESS);
    assert(rt.sets == std::vector<XrActionSet>({appSet, privateSet}));
    assert(rt.attachNext == &marker && ai.countActionSets == 1 && ai.actionSets == &appSet);
    XrActiveActionSet a{appSet, 123};
    XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO, &marker, 1, &a};
    assert(gaze.sync(session, &si) == XR_SUCCESS);
    assert(rt.activeSets.size() == 2 && rt.activeSets[0].actionSet == appSet && rt.activeSets[0].subactionPath == 123);
    assert(rt.activeSets[1].actionSet == privateSet && rt.activeSets[1].subactionPath == XR_NULL_PATH);
    assert(rt.syncNext == &marker && si.countActiveActionSets == 1 && si.activeActionSets == &a);
    XrPosef out{}; assert(gaze.sample(baseSpace, 987, out));
    assert(out.position.x == 1 && out.position.y == 2 && rt.locatedTime == 987 && rt.locatedBase == baseSpace);
    assert(rt.syncs == 1); // Never issue a private-only sync that deactivates the game's actions.
    gaze.resetSession(); assert(rt.destroyed == std::vector<int>({5, 3}));
    gaze.resetSession(); assert(rt.destroyed.size() == 2);
}
void failed_and_inactive_samples_never_reuse_pose() {
    rt = {}; lmuqv::VrsGaze gaze; initialize(gaze); appAttachAndSync(gaze);
    XrPosef out{}; assert(gaze.sample(baseSpace, 100, out));
    rt.active = false; assert(!gaze.sample(baseSpace, 101, out)); assert(rt.locates == 1);
    rt.active = true; rt.poseResult = XR_ERROR_RUNTIME_FAILURE; assert(!gaze.sample(baseSpace, 102, out));
    rt.poseResult = XR_SUCCESS; rt.locateResult = XR_ERROR_TIME_INVALID; assert(!gaze.sample(baseSpace, 103, out));
    rt.locateResult = XR_SUCCESS; rt.flags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    assert(!gaze.sample(baseSpace, 104, out));
    rt.flags = XR_SPACE_LOCATION_POSITION_VALID_BIT; assert(!gaze.sample(baseSpace, 105, out));
    rt.flags |= XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    rt.pose.orientation.w = std::numeric_limits<float>::quiet_NaN(); assert(!gaze.sample(baseSpace, 106, out));
    rt.pose.orientation = {0, 0, 0, 0}; assert(!gaze.sample(baseSpace, 107, out));
    rt.pose.orientation.w = 1; rt.pose.position.x = std::numeric_limits<float>::infinity();
    assert(!gaze.sample(baseSpace, 108, out));
    rt.pose.position.x = 1; assert(gaze.sample(baseSpace, 109, out));
    XrActionsSyncInfo si{}; si.type = XR_TYPE_ACTIONS_SYNC_INFO; rt.syncResult = XR_SESSION_NOT_FOCUSED;
    assert(gaze.sync(session, &si) == XR_SESSION_NOT_FOCUSED);
    const auto gets = rt.gets; assert(!gaze.sample(baseSpace, 110, out)); assert(rt.gets == gets);
    rt.syncResult = XR_ERROR_RUNTIME_FAILURE;
    assert(gaze.sync(session, &si) == XR_ERROR_RUNTIME_FAILURE);
    assert(!gaze.sample(baseSpace, 111, out)); assert(rt.gets == gets);
    rt.syncResult = XR_SUCCESS; assert(gaze.sync(session, &si) == XR_SUCCESS);
    assert(gaze.sample(baseSpace, 112, out));
    gaze.resetSession();
}
void lazy_attach_is_once_and_late_app_errors_are_preserved() {
    rt = {}; lmuqv::VrsGaze gaze; initialize(gaze);
    assert(gaze.ensureAttachedAndSync(session));
    assert(rt.sets == std::vector<XrActionSet>({privateSet}) && rt.syncs == 1);
    XrPosef out{}; assert(gaze.sample(baseSpace, 10, out)); assert(rt.syncs == 2 && rt.attaches == 1);
    XrSessionActionSetsAttachInfo ai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO, nullptr, 1, &appSet};
    assert(gaze.attach(session, &ai) == XR_ERROR_ACTIONSETS_ALREADY_ATTACHED);
    rt.syncResult = XR_ERROR_RUNTIME_FAILURE;
    assert(!gaze.sample(baseSpace, 11, out));
    gaze.resetSession();

    rt = {}; initialize(gaze); rt.attachResult = XR_ERROR_RUNTIME_FAILURE;
    assert(!gaze.ensureAttachedAndSync(session)); assert(!gaze.ensureAttachedAndSync(session));
    assert(rt.attaches == 1 && rt.syncs == 0); gaze.resetSession();
}
void app_errors_are_passthrough_and_prevent_lazy_attach() {
    rt = {}; lmuqv::VrsGaze gaze; initialize(gaze);
    XrSessionActionSetsAttachInfo invalid{}; invalid.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
    assert(gaze.attach(session, &invalid) == XR_ERROR_VALIDATION_FAILURE);
    assert(rt.attachPointer == &invalid);
    assert(!gaze.ensureAttachedAndSync(session)); assert(rt.attaches == 1);
    XrActionsSyncInfo invalidSync{XR_TYPE_ACTIONS_SYNC_INFO, nullptr, 1, nullptr};
    assert(gaze.sync(session, &invalidSync) == XR_ERROR_VALIDATION_FAILURE);
    assert(rt.syncPointer == &invalidSync);
    XrPosef out{}; assert(!gaze.sample(baseSpace, 100, out)); assert(rt.gets == 0);
    assert(gaze.attach(handle<XrSession>(99), &invalid) == XR_ERROR_HANDLE_INVALID);
    gaze.resetSession();
}
void failed_app_attach_can_retry_without_private_attach() {
    rt = {}; lmuqv::VrsGaze gaze; initialize(gaze);
    XrSessionActionSetsAttachInfo ai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO, nullptr, 1, &appSet};
    rt.attachResult = XR_ERROR_RUNTIME_FAILURE;
    assert(gaze.attach(session, &ai) == XR_ERROR_RUNTIME_FAILURE);
    assert(!gaze.ensureAttachedAndSync(session)); assert(rt.attaches == 1);
    XrPosef out{}; assert(!gaze.sample(baseSpace, 1, out)); assert(rt.gets == 0);
    rt.attachResult = XR_SUCCESS; appAttachAndSync(gaze);
    assert(gaze.sample(baseSpace, 2, out)); assert(rt.attaches == 2);
    assert(!gaze.ensureAttachedAndSync(handle<XrSession>(99)));
    assert(!gaze.sample(XR_NULL_HANDLE, 3, out));
    gaze.resetSession();
}
void optional_and_failed_initialization_fall_back_cleanly() {
    rt = {}; lmuqv::VrsGaze gaze; XrPosef out{};
    assert(!gaze.sample(baseSpace, 1, out)); assert(!gaze.ensureAttachedAndSync(session)); assert(rt.creates == 0);
    rt.missing = "xrGetActionStatePose"; assert(!gaze.load(instance, getProc));
    assert(!gaze.initialize(session)); assert(rt.creates == 0);
    XrSessionActionSetsAttachInfo ai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO, nullptr, 1, &appSet};
    assert(gaze.attach(session, &ai) == XR_SUCCESS);
    assert(rt.attachPointer == &ai && rt.sets == std::vector<XrActionSet>({appSet}));
    XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO, nullptr, 0, nullptr};
    assert(gaze.sync(session, &si) == XR_SUCCESS && rt.syncPointer == &si);
    rt = {}; assert(gaze.load(instance, getProc)); rt.bindingResult = XR_ERROR_PATH_UNSUPPORTED;
    assert(!gaze.initialize(session)); assert(rt.destroyed == std::vector<int>({3}));
    assert(!gaze.sample(baseSpace, 1, out)); assert(rt.gets == 0);
    rt = {}; assert(gaze.load(instance, getProc)); rt.spaceResult = XR_ERROR_RUNTIME_FAILURE;
    assert(!gaze.initialize(session)); assert(rt.destroyed == std::vector<int>({3}));
    rt = {}; initialize(gaze); gaze.resetSession();
    rt = {}; assert(gaze.initialize(session)); assert(gaze.ensureAttachedAndSync(session)); gaze.resetSession();
}
}
int main() {
    append_preserves_application_input(); failed_and_inactive_samples_never_reuse_pose();
    lazy_attach_is_once_and_late_app_errors_are_preserved(); app_errors_are_passthrough_and_prevent_lazy_attach();
    failed_app_attach_can_retry_without_private_attach();
    optional_and_failed_initialization_fall_back_cleanly();
    std::cout << "VRS gaze: attachment, app input preservation, freshness, fallback and lifecycle passed\n";
}
