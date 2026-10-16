#include "hooks.h"
#include "utils.h"

#define OPENVR_INTERFACE_INTERNAL
#include "openvr_1_7_15.h"
namespace vrnew = vr_1_7_15;

namespace {
    constexpr size_t slotIdx_IVRSystem_PollNextEvent = 30;
    constexpr size_t slotIdx_IVROverlay_PollNextOverlayEvent = 45;
    constexpr size_t slotIdx_IVROverlay_ShowKeyboardForOverlay = 73;
    constexpr size_t slotIdx_IVROverlay_GetKeyboardText = 74;
    constexpr size_t slotIdx_IVROverlay_HideKeyboard = 75;

    typedef bool (*pfnPollNextEvent)(vrnew::IVRSystem *, vrnew::VREvent_t *, uint32_t);

    typedef bool (*pfnPollNextOverlayEvent)(vr::IVROverlay *, vr::VROverlayHandle_t, vr::VREvent_t *, uint32_t);
    typedef vr::EVROverlayError (*pfnShowKeyboardForOverlay)(vr::IVROverlay *, vr::VROverlayHandle_t, vr::EGamepadTextInputMode, vr::EGamepadTextInputLineMode, const char *, uint32_t, const char *, bool, uint64_t);
    typedef uint32_t (*pfnGetKeyboardText)(vr::IVROverlay *, char *, uint32_t);
    typedef void (*pfnHideKeyboard)(vr::IVROverlay *);

    pfnPollNextEvent origPollNextEvent;
    pfnPollNextOverlayEvent origPollNextOverlayEvent;
    pfnShowKeyboardForOverlay origShowKeyboardForOverlay;
    pfnGetKeyboardText origGetKeyboardText;
    pfnHideKeyboard origHideKeyboard;

    vr::IVROverlay* vrOverlay;
    vr::VROverlayHandle_t currentOverlayHandle = vr::k_ulOverlayHandleInvalid;
    std::deque<vr::VREvent_t> redirectedEvents; // the events from a non-overlay keyboard we collected
    std::string accumulatedText;

    void PopUtf8Char(std::string& text) {
        while (!text.empty()) {
            auto c = text.back();
            text.pop_back();
            if ((c & 0xC0) != 0x80) break;
        }
    }

    bool HandleKeyboardEvent(vrnew::VREvent_t& event) {
        bool isKeyboardEvent = event.eventType == vrnew::VREvent_KeyboardDone || event.eventType == vrnew::VREvent_KeyboardClosed || event.eventType == vrnew::VREvent_KeyboardCharInput;
        if (!isKeyboardEvent) return false;

        if (currentOverlayHandle == vr::k_ulOverlayHandleInvalid) return true; // return as handled but do nothing

        vr::VREvent_t overlayEvent = {
            .eventType = event.eventType,
            .trackedDeviceIndex = event.trackedDeviceIndex,
            .eventAgeSeconds = event.eventAgeSeconds,
        };
        static_assert(sizeof(vr::VREvent_Keyboard_t) == sizeof(vrnew::VREvent_Keyboard_t));
        memcpy(&overlayEvent.data.keyboard, &event.data.keyboard, sizeof(vr::VREvent_Keyboard_t));

        redirectedEvents.emplace_back(overlayEvent);

        if (event.eventType == vrnew::VREvent_KeyboardDone || event.eventType == vrnew::VREvent_KeyboardClosed) {
            currentOverlayHandle = vr::k_ulOverlayHandleInvalid;
        } else if (event.eventType == vrnew::VREvent_KeyboardCharInput) {
            // uhh, cNewInput seems empty, and GetKeyboardText seems to contain the last input, independent if the keyboard is in the minimal mode or not,
            // so we process it as such, i guess
            constexpr uint32_t bufLen = 256;
            char buf[bufLen];
            uint32_t len = origGetKeyboardText(vrOverlay, buf, bufLen);
            std::string_view input(buf, len);
            for (char c : input) {
                if (c == '\b') PopUtf8Char(accumulatedText); // backspace
                else accumulatedText += c;
            }
        }
        return true;
    }

    bool Hook_PollNextEvent(vrnew::IVRSystem *self, vrnew::VREvent_t *pEvent, uint32_t) {
        while (true) {
            vrnew::VREvent_t event{};
            auto result = origPollNextEvent(self, &event, sizeof(event));
            if (!result) return false;
            if (HandleKeyboardEvent(event)) continue; // poll the next non-keyboard event
            if (pEvent) *pEvent = event;
            return true;
        }
    }

    bool Hook_PollNextOverlayEvent(vr::IVROverlay *self, vr::VROverlayHandle_t ulOverlayHandle, vr::VREvent_t *pEvent, uint32_t uncbVREvent) {
        bool forKeyboardOverlay = ulOverlayHandle == currentOverlayHandle;
        if (forKeyboardOverlay && !redirectedEvents.empty()) {
            if (pEvent) *pEvent = redirectedEvents.front();
            redirectedEvents.pop_front();
            return true;
        }

        while (true) {
            vr::VREvent_t event{};
            bool origResult = origPollNextOverlayEvent(self, ulOverlayHandle, &event, uncbVREvent);
            if (origResult) {
                // i don't know why, but i get char input events (but not close/done) from PollNextOverlayEvent as well, so skip them to avoid duplicates
                bool isKeyboardEvent = event.eventType == vr::VREvent_KeyboardCharInput || event.eventType == vr::VREvent_KeyboardClosed || event.eventType == vr::VREvent_KeyboardDone;
                if (forKeyboardOverlay && isKeyboardEvent) continue;
            }
            if (pEvent) *pEvent = event;
            return origResult;
        }
    }

    vr::EVROverlayError Hook_ShowKeyboardForOverlay(vr::IVROverlay *self, vr::VROverlayHandle_t ulOverlayHandle, vr::EGamepadTextInputMode eInputMode, vr::EGamepadTextInputLineMode eLineInputMode, const char *pchDescription, uint32_t unCharMax, const char *pchExistingText, bool bUseMinimalMode, uint64_t uUserValue) {
        currentOverlayHandle = vr::k_ulOverlayHandleInvalid;

        auto error = self->ShowKeyboard(eInputMode, eLineInputMode, pchDescription, unCharMax, pchExistingText, bUseMinimalMode, uUserValue);
        if (error != vr::VROverlayError_None) {
            logger::warn("IVROverlay::ShowKeyboard failed with error {}, falling back to IVROverlay::ShowKeyboardForOverlay", (int) error);
            return origShowKeyboardForOverlay(self, ulOverlayHandle, eInputMode, eLineInputMode, pchDescription, unCharMax, pchExistingText, bUseMinimalMode, uUserValue);
        }

        currentOverlayHandle = ulOverlayHandle;
        if (pchExistingText) accumulatedText.assign(pchExistingText);
        else accumulatedText.clear();
        return error;
    }

    uint32_t Hook_GetKeyboardText(vr::IVROverlay *self, char *pchText, uint32_t cchText) {
        uint32_t lengthWithoutNul = 0;
        if (pchText && cchText > 0) {
            lengthWithoutNul = min(accumulatedText.size(), cchText - 1);
            memcpy(pchText, accumulatedText.data(), lengthWithoutNul);
            pchText[lengthWithoutNul] = '\0';
        }
        return lengthWithoutNul;
    }

    void Hook_HideKeyboard(vr::IVROverlay *self) {
        currentOverlayHandle = vr::k_ulOverlayHandleInvalid;
        origHideKeyboard(self);
    }
}

bool InstallHooks() {
    // Skyrim VR works with IVRSystem_019, but with SKSE it's a wrapper over IVRSystem_020, so let's hook the real thing
    auto vrSystem = (vrnew::IVRSystem *) GetRealGenericVRInterface(vrnew::IVRSystem_Version);
    if (!vrSystem) return false;

    vrOverlay = (vr::IVROverlay *) GetRealGenericVRInterface(vr::IVROverlay_Version);
    if (!vrOverlay) return false;

    REL::Relocation<uintptr_t> vtable_vrSystem{*((uintptr_t *) vrSystem)};
    origPollNextEvent = InstallHook(vtable_vrSystem, slotIdx_IVRSystem_PollNextEvent, &Hook_PollNextEvent, "IVRSystem::PollNextEvent");

    REL::Relocation<uintptr_t> vtable_vrOverlay{*((uintptr_t *) vrOverlay)};
    origPollNextOverlayEvent = InstallHook(vtable_vrOverlay, slotIdx_IVROverlay_PollNextOverlayEvent, &Hook_PollNextOverlayEvent, "IVROverlay::PollNextOverlayEvent");
    origShowKeyboardForOverlay = InstallHook(vtable_vrOverlay, slotIdx_IVROverlay_ShowKeyboardForOverlay, &Hook_ShowKeyboardForOverlay, "IVROverlay::ShowKeyboardForOverlay");
    origGetKeyboardText = InstallHook(vtable_vrOverlay, slotIdx_IVROverlay_GetKeyboardText, &Hook_GetKeyboardText, "IVROverlay::GetKeyboardText");
    origHideKeyboard = InstallHook(vtable_vrOverlay, slotIdx_IVROverlay_HideKeyboard, &Hook_HideKeyboard, "IVROverlay::HideKeyboard");

    return true;
}
