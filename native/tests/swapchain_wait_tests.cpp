#include "../src/layer.cpp"
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
const auto instanceHandle = reinterpret_cast<XrInstance>(101);
const auto sessionHandle = reinterpret_cast<XrSession>(102);
const auto chainHandle = reinterpret_cast<XrSwapchain>(103);
const auto otherChain = reinterpret_cast<XrSwapchain>(104);
HANDLE entered{}, resumeWait{}, progressed{};
HANDLE secondEntered{}, secondResume{};
XrResult expectedResult = XR_SUCCESS;
XrResult destroyResult = XR_SUCCESS;
unsigned destroyCalls{}, acquireCalls{}, releaseCalls{};
bool callbackSafe{};
XrResult XRAPI_CALL destroyInstance(XrInstance) { ++destroyCalls; return destroyResult; }
XrResult XRAPI_CALL destroySession(XrSession) { ++destroyCalls; return destroyResult; }
XrResult XRAPI_CALL destroyChain(XrSwapchain) { ++destroyCalls; return destroyResult; }
XrResult XRAPI_CALL acquire(XrSwapchain, const XrSwapchainImageAcquireInfo*, uint32_t* index) { ++acquireCalls; *index = 7; return XR_SUCCESS; }
XrResult XRAPI_CALL release(XrSwapchain, const XrSwapchainImageReleaseInfo*) { ++releaseCalls; return XR_SUCCESS; }
XrResult XRAPI_CALL blockedWait(XrSwapchain, const XrSwapchainImageWaitInfo*) {
    SetEvent(entered);
    return WaitForSingleObject(resumeWait, 3000) == WAIT_OBJECT_0 ? expectedResult : XR_ERROR_RUNTIME_FAILURE;
}
XrResult XRAPI_CALL immediateWait(XrSwapchain, const XrSwapchainImageWaitInfo*) { return expectedResult; }
XrResult XRAPI_CALL parallelWait(XrSwapchain chain, const XrSwapchainImageWaitInfo*) {
    SetEvent(chain == chainHandle ? entered : secondEntered);
    return WaitForSingleObject(chain == chainHandle ? resumeWait : secondResume, 3000) == WAIT_OBJECT_0 ? XR_SUCCESS : XR_ERROR_RUNTIME_FAILURE;
}
XrResult XRAPI_CALL throwingWait(XrSwapchain, const XrSwapchainImageWaitInfo*) { throw std::runtime_error("fixture runtime exception"); }
XrResult XRAPI_CALL reentrantWait(XrSwapchain, const XrSwapchainImageWaitInfo*) {
    uint32_t index{};
    callbackSafe = XR_FAILED(destroy_swapchain(chainHandle)) && XR_FAILED(destroy_session(sessionHandle)) &&
        XR_FAILED(destroy_instance(instanceHandle)) && XR_FAILED(wait_image(chainHandle, nullptr)) &&
        XR_FAILED(acquire_image(chainHandle, nullptr, &index)) && XR_FAILED(release_image(chainHandle, nullptr));
    return XR_SUCCESS;
}
void fixture(PFN_xrWaitSwapchainImage wait) {
    chains.clear(); sessions.clear(); instances.clear();
    auto i = std::make_unique<Instance>(); i->handle = instanceHandle;
    i->DestroyInstance = destroyInstance; i->DestroySession = destroySession; i->DestroySwapchain = destroyChain;
    i->AcquireSwapchainImage = acquire; i->ReleaseSwapchainImage = release; i->WaitSwapchainImage = wait;
    auto s = std::make_unique<Session>(); s->instance = i.get(); s->handle = sessionHandle;
    for (const auto h : {chainHandle, otherChain}) {
        auto c = std::make_unique<Swapchain>(); c->session = s.get(); c->acquired.emplace_back(3, false);
        chains[h] = std::move(c);
    }
    instances[instanceHandle] = std::move(i); sessions[sessionHandle] = std::move(s);
    destroyCalls = acquireCalls = releaseCalls = 0;
    destroyResult = XR_SUCCESS;
}
void progressAndTeardown() {
    fixture(blockedWait);
    entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    resumeWait = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    progressed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    check(entered && resumeWait && progressed, "fixture events");
    XrResult result = XR_ERROR_RUNTIME_FAILURE, unrelated = XR_ERROR_RUNTIME_FAILURE;
    std::thread waiting([&] { result = wait_image(chainHandle, nullptr); });
    const bool started = WaitForSingleObject(entered, 2000) == WAIT_OBJECT_0;
    std::thread other([&] { uint32_t index{}; unrelated = acquire_image(otherChain, nullptr, &index); SetEvent(progressed); });
    const bool independent = started && WaitForSingleObject(progressed, 1000) == WAIT_OBJECT_0;
    // Do not enter a destroy hook until the lock test succeeds: the original
    // serialized implementation must fail cleanly without hanging the fixture.
    bool protectedLifetime = false;
    if (independent) {
        protectedLifetime = XR_FAILED(destroy_swapchain(chainHandle)) && XR_FAILED(destroy_session(sessionHandle)) && XR_FAILED(destroy_instance(instanceHandle));
        protectedLifetime = protectedLifetime && destroyCalls == 0;
        uint32_t index{};
        protectedLifetime = protectedLifetime && XR_FAILED(acquire_image(chainHandle, nullptr, &index)) && XR_FAILED(release_image(chainHandle, nullptr)) && XR_FAILED(wait_image(chainHandle, nullptr));
    }
    SetEvent(resumeWait); waiting.join(); other.join();
    CloseHandle(entered); CloseHandle(resumeWait); CloseHandle(progressed);
    check(independent && unrelated == XR_SUCCESS, "unrelated swapchain must progress while runtime wait blocks");
    check(protectedLifetime, "in-flight wait must pin swapchain and both parents without blocking teardown callbacks");
    check(result == XR_SUCCESS && chains.at(chainHandle)->acquired.front().second, "successful wait marks its image ready");
    check(chains.at(otherChain)->acquired.size() == 2 && !chains.at(otherChain)->acquired.front().second, "unrelated acquire preserves wait queue");
    check(destroy_swapchain(chainHandle) == XR_SUCCESS && chains.count(chainHandle) == 0, "destroy swapchain retries after unpin");
    check(destroy_session(sessionHandle) == XR_SUCCESS && chains.empty(), "destroy session retries after unpin");
    check(destroy_instance(instanceHandle) == XR_SUCCESS && instances.empty(), "destroy instance retries after unpin");
}
void concurrentParentPins() {
    fixture(parallelWait);
    entered = CreateEventW(nullptr, TRUE, FALSE, nullptr); resumeWait = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    secondEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr); secondResume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    check(entered && resumeWait && secondEntered && secondResume, "parallel fixture events");
    XrResult first{}, second{};
    std::thread a([&] { first = wait_image(chainHandle, nullptr); });
    std::thread b([&] { second = wait_image(otherChain, nullptr); });
    const bool bothEntered = WaitForSingleObject(entered, 1000) == WAIT_OBJECT_0 && WaitForSingleObject(secondEntered, 1000) == WAIT_OBJECT_0;
    SetEvent(resumeWait); a.join();
    const bool parentPinned = bothEntered && XR_FAILED(destroy_session(sessionHandle)) && XR_FAILED(destroy_instance(instanceHandle)) &&
        destroy_swapchain(chainHandle) == XR_SUCCESS && XR_FAILED(destroy_swapchain(otherChain));
    SetEvent(secondResume); b.join();
    CloseHandle(entered); CloseHandle(resumeWait); CloseHandle(secondEntered); CloseHandle(secondResume);
    check(parentPinned && first == XR_SUCCESS && second == XR_SUCCESS, "one completed wait cannot unpin parents of another active wait");
    check(destroy_instance(instanceHandle) == XR_SUCCESS, "all wait completions release parent pins");
}
}
int main() {
    try {
        progressAndTeardown();
        concurrentParentPins();
        for (const auto result : {XR_TIMEOUT_EXPIRED, XR_ERROR_RUNTIME_FAILURE, XR_ERROR_SESSION_LOST}) {
            fixture(immediateWait); expectedResult = result;
            check(wait_image(chainHandle, nullptr) == result, "runtime result preserved");
            check(!chains.at(chainHandle)->acquired.front().second, "timeout and failure never mark an image ready");
            check(destroy_instance(instanceHandle) == XR_SUCCESS && chains.empty() && sessions.empty(), "failed waits release parent pins");
        }
        fixture(throwingWait);
        check(wait_image(chainHandle, nullptr) == XR_ERROR_RUNTIME_FAILURE, "runtime exception contained at OpenXR boundary");
        check(!chains.at(chainHandle)->acquired.front().second && destroy_instance(instanceHandle) == XR_SUCCESS, "exception releases pins without marking image ready");
        fixture(reentrantWait);
        check(wait_image(chainHandle, nullptr) == XR_SUCCESS && callbackSafe && destroyCalls == 0 && acquireCalls == 0 && releaseCalls == 0, "same-thread callbacks reject conflicting operations without deadlock");
        check(destroy_instance(instanceHandle) == XR_SUCCESS, "reentrant callbacks leave teardown retryable");
        fixture(immediateWait); expectedResult = XR_SUCCESS;
        chains.at(chainHandle)->acquired.front().second = true;
        chains.at(chainHandle)->acquired.emplace_back(4, false); chains.at(chainHandle)->acquired.emplace_back(5, false);
        check(wait_image(chainHandle, nullptr) == XR_SUCCESS && chains.at(chainHandle)->acquired[1].second && !chains.at(chainHandle)->acquired[2].second, "success marks only the next unwaited acquired image");
        destroyResult = XR_ERROR_RUNTIME_FAILURE;
        check(XR_FAILED(destroy_swapchain(chainHandle)) && XR_FAILED(destroy_session(sessionHandle)) && XR_FAILED(destroy_instance(instanceHandle)), "downstream destroy failures propagate");
        check(chains.size() == 2 && sessions.size() == 1 && instances.size() == 1, "failed teardown preserves ownership");
        destroyResult = XR_SUCCESS; check(destroy_instance(instanceHandle) == XR_SUCCESS, "failed teardown remains retryable");
        std::cout << "Swapchain wait concurrency, lifetime, callback, timeout and exception tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
