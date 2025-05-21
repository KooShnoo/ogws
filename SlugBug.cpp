#include "egg/core/eggAllocator.h"
#include "egg/core/eggDisplay.h"
#include "egg/core/eggExpHeap.h"
#include "egg/core/eggGraphicsFifo.h"
#include "egg/core/eggHeap.h"
#include "egg/core/eggSystem.h"
#include "egg/core/eggVideo.h"
#include "egg/core/eggXfb.h"
#include "egg/core/eggXfbManager.h"
#include "egg/math/eggMatrix.h"
#include "egg/math/eggVector.h"
#include "g3d/g3d_camera.h"
#include "g3d/g3d_scnobj.h"
#include "nw4r/g3d/g3d_init.h"
#include "nw4r/g3d/g3d_scnmdlsmpl.h"
#include "nw4r/g3d/g3d_scnroot.h"
#include "nw4r/g3d/g3d_state.h"
#include "nw4r/g3d/res/g3d_resfile.h"
#include "nw4r/g3d/res/g3d_resmdl.h"
#include "nw4r/ut/ut_Color.h"
#include "revolution/OS/OSArena.h"
#include "revolution/OS/OSError.h"
#include "revolution/OS/OSReset.h"
#include "revolution/OS/OSThread.h"
#include "revolution/VI/vi.h"
#include "revolution/WPAD/WPAD.h"

// wip
const u32 WPAD_RINGBUFFER_LEN = 128;
static WPADFSStatus s_ringbuffer[WPAD_RINGBUFFER_LEN];
static EGG::Allocator *s_WPADAllocator;
static s32 s_controller_type = WPAD_DEV_NONE;
bool s_did_request_shutdown = false;
bool s_did_request_reset = false;
static union {
    struct {
        WPADStatus wiimote;
    };
    WPADFSStatus wiichuck;
} s_controller_status;

class InputManager {
public:
    static void *WPADAllocator(u32 size) { return s_WPADAllocator->alloc(size); }
    static int WPADFree(void *block) {
        s_WPADAllocator->free(block);
        return 1;
    }

    static bool isConnected() { return s_controller_type < WPAD_DEV_NONE; }

    static void onExtension(s32 player_num, s32 dev) {
        if (WPADGetStatus() != WPAD_LIB_STATUS_3) { return; }

        OSReport("onExtension()! player %ld, ext=%ld", player_num + 1, dev);
    }

    static void onConnect(s32 chan, s32 result) {
        if (WPADGetStatus() != WPAD_LIB_STATUS_3) { return; }

        if (chan != WPAD_CHAN0) {
            OSReport("hmm only one controller supported, sry :(\n");
            return;
        }

        if (result == WPAD_ERR_OK) {
            OSReport("A controller has been connected, but it is probably still initializng the connection.\n");
        } else if (result == WPAD_ERR_NO_CONTROLLER) {
            OSReport("disconnected controlelr.\n");
            s_controller_type = WPAD_DEV_NONE;
        } else {
            OSReport("onConnect(): wpaderr[%ld].\n", result);
            s_controller_type = WPAD_DEV_NONE;
        }
    }

    static void init(EGG::Allocator *pWPADAllocator) {

        s_WPADAllocator = pWPADAllocator;

        WPADRegisterAllocator(&WPADAllocator, &WPADFree);
        WPADInit();
        WPADSetConnectCallback(WPAD_CHAN0, &onConnect);
        WPADSetExtensionCallback(0, &onExtension);

        // i cheated to write this code.
        while (WPADGetStatus() != WPAD_LIB_STATUS_3) {}
    }

    static void calc() {
        s32 old_controller_type = s_controller_type;

        s32 wpadErr = WPADProbe(WPAD_CHAN0, &s_controller_type);
        if (wpadErr != WPAD_ERR_OK) {
            s_controller_type = WPAD_DEV_NONE;
            return;
        }

        s32 new_controller_type = s_controller_type;
        bool controllerChanged = new_controller_type != old_controller_type;
        if (controllerChanged) {
            if (new_controller_type == WPAD_DEV_INITIALIZING) {
                OSReport("initializing connection...\n");
            } else if (new_controller_type == WPAD_DEV_CORE) {
                OSReport("connected to wiimote.\n");
                WPADSetDataFormat(WPAD_CHAN0, WPAD_FMT_CORE_BTN_ACC_DPD);
                WPADSetAutoSamplingBuf(WPAD_CHAN0, &s_ringbuffer, WPAD_RINGBUFFER_LEN);
            } else if (new_controller_type == WPAD_DEV_FS) {
                OSReport("connected to wiichuck.\n");
                WPADSetDataFormat(WPAD_CHAN0, WPAD_FMT_FS_BTN_ACC_DPD);
                WPADSetAutoSamplingBuf(WPAD_CHAN0, &s_ringbuffer, WPAD_RINGBUFFER_LEN);
            } else {
                OSReport("connected to unknown controller %ld.\n", new_controller_type);
            }
        }

        if (isConnected()) { WPADRead(WPAD_CHAN0, &s_controller_status.wiimote); }
    }

    static inline bool pressedButton(s32 button) { return s_controller_status.wiimote.button & button; }
};

void *OSAllocFromMEM2ArenaLo(size_t size, u32 align) {
    u8 *begin = (u8 *)ROUND_UP_PTR(OSGetMEM2ArenaLo(), align);
    u8 *end = begin + size;
    end = (u8 *)ROUND_UP_PTR(end, align);
    OSSetMEM2ArenaLo(end);
    return begin;
}

inline void MTX34SetPos(nw4r::math::MTX34 &m, const EGG::Vector3f &v) {
    m._03 = v.x;
    m._13 = v.y;
    m._23 = v.z;
}

void printMatrix(const nw4r::math::MTX34 &m) {
    OSReport("<nw4r::math::MTX34>\n");
    for (u32 i = 0; i < 3; i++) {
        for (u32 j = 0; j < 4; j++) {
            OSReport("%f.0, ", m.a[j + 4 * i]);
        }
        OSReport("\n");
    }
    OSReport("</nw4r::math::MTX34>\n");
}

// for debugging
#pragma dont_inline on
// #pragma clang diagnostic ignored "-Wc++11-extensions"
// #define let auto

// dtk vfs cp orig/RSPE01_01/wsprts.wbfs:files/Common/RPBowScene/common.carc:G3D/bwg_Pin.brres bowling_pin.brres
// powerpc-eabi-objcopy bowling_pin.brres bowling_pin.o -I binary -O elf32-powerpc
extern u8 _binary_bowling_pin_brres_start[];
extern u8 _binary_bowling_pin_brres_end[];
static u8 *bowling_pin_rres = (u8 *)&_binary_bowling_pin_brres_start;

u32 frames = 0;
const u32 heap_size = 12 * 1024 * 1024; // 12MiB
const u32 gpu_fifo_size = 256 * 1024;   // 256KiB

class SlugBugSystem : public EGG::ConfigurationData {
public:
    EGG::Video *getVideo() override { return m_video; }
    UNKTYPE VIRT_0xC(UNKTYPE) override {}
    EGG::Display *getDisplay() override { return m_display; }
    EGG::XfbManager *getXfbMgr() override { return m_xfb_mgr; }

    EGG::Video *m_video;
    EGG::Display *m_display;
    EGG::XfbManager *m_xfb_mgr;
    EGG::Heap *m_heap;
    EGG::Heap *m_mem2_heap;

    static void onPowerButton() { s_did_request_shutdown = true; }
    static void onResetButton() { s_did_request_reset    = true; }

    void init_heap() {
        void *heap_base = OSAllocFromMEM1ArenaLo(heap_size, 32);
        ASSERT(heap_base);
        EGG::Heap::initialize();
        m_heap = EGG::ExpHeap::create(heap_base, heap_size, 0);
        m_heap->becomeCurrentHeap();

        heap_base = OSAllocFromMEM2ArenaLo(heap_size, 32);
        ASSERT(heap_base);
        EGG::Heap::initialize();
        m_mem2_heap = EGG::ExpHeap::create(heap_base, heap_size, 0);
    }

    void slugBug() {
        EGG::BaseSystem::mConfigData = this;

        init_heap();
        EGG::Allocator allocator = EGG::Allocator(m_heap, 32);
        EGG::Allocator mem2_allocator = EGG::Allocator(m_mem2_heap, 32);

        OSSetPowerCallback(&onPowerButton);
        OSSetResetCallback(&onResetButton);

        InputManager::init(&mem2_allocator);

        EGG::GraphicsFifo::create(gpu_fifo_size, nullptr);

        m_video = new EGG::Video(nullptr);

        m_xfb_mgr = new EGG::XfbManager(nullptr);
        m_xfb_mgr->attach(new EGG::Xfb(nullptr));
        m_xfb_mgr->attach(new EGG::Xfb(nullptr));

        m_display = new EGG::Display(1);

        nw4r::g3d::G3dInit(true);
        nw4r::g3d::G3DState::SetRenderModeObj(*m_video->mRenderMode);

        nw4r::g3d::ResFile pin_file = nw4r::g3d::ResFile(bowling_pin_rres);
        pin_file.Init();
        ASSERT(pin_file.Bind());
        nw4r::g3d::ResMdl pin_mdl = pin_file.GetResMdl(0);
        pin_mdl.Init();

        nw4r::g3d::ScnRoot *scene_3d = nw4r::g3d::ScnRoot::Construct(&allocator, NULL, 32, 32);
        nw4r::g3d::ScnMdlSimple *scene_pin = nw4r::g3d::ScnMdlSimple::Construct(&allocator, NULL, pin_mdl, 1);
        scene_3d->PushBack(scene_pin);

        nw4r::g3d::Camera camera = scene_3d->GetCurrentCamera();
        camera.SetPosition(15.0f, 10.0f, 15.0f);

        nw4r::math::MTX34 model_matrix;
        scene_pin->GetMtx(nw4r::g3d::ScnObj::MTX_LOCAL, &model_matrix);
        EGG::Vector3f pos = EGG::Vector3f(model_matrix._03, model_matrix._13, model_matrix._23);

        u8 r = 0x1f;
        u8 g = 0x1f;
        u8 b = 0x1f;
        m_display->setClearColor(nw4r::ut::Color(r, g, b, 0xff));
        setBlack(false);

        // i guess some 'display modes' want this, or something
        VIWaitForRetrace();
        VIWaitForRetrace();
        VIWaitForRetrace();

        OSReport("entering main loop.\n");
        while (true) {
            m_display->beginFrame();
            m_display->beginRender();

            if (s_did_request_shutdown) { OSShutdownSystem(); }
            // OSRestart is missing for some reason
            if (s_did_request_reset)    { OSReturnToMenu();   }

            InputManager::calc();

            if (InputManager::pressedButton(WPAD_BUTTON_RIGHT)) { pos.x += 0.1f; }
            if (InputManager::pressedButton(WPAD_BUTTON_PLUS))  { pos.y += 0.1f; }
            if (InputManager::pressedButton(WPAD_BUTTON_UP))    { pos.z += 0.1f; }
            if (InputManager::pressedButton(WPAD_BUTTON_LEFT))  { pos.x -= 0.1f; }
            if (InputManager::pressedButton(WPAD_BUTTON_MINUS)) { pos.y -= 0.1f; }
            if (InputManager::pressedButton(WPAD_BUTTON_DOWN))  { pos.z -= 0.1f; }
            MTX34SetPos(model_matrix, pos);
            scene_pin->SetMtx(nw4r::g3d::ScnObj::MTX_LOCAL, &model_matrix);

            if (InputManager::pressedButton(WPAD_BUTTON_HOME)) {
                // todo: use nintendo's HBM (Home Button Menu) library
                OSReport("home pressed, returning home.\n");
                OSReturnToMenu();
            }

            // calc
            scene_3d->CalcWorld();
            scene_3d->CalcMaterial();
            scene_3d->CalcView();
            scene_3d->GatherDrawScnObj();
            scene_3d->ZSort();

            // draw
            scene_3d->DrawOpa();
            scene_3d->DrawXlu();

            m_display->endRender();
            m_display->endFrame();
        }
    }

    void setBlack(bool black) {
        VISetBlack(black);
        m_display->setBlack(black);
    }
};

extern "C" {
void slugBug() {
    SlugBugSystem slug_bug_system = SlugBugSystem();
    slug_bug_system.slugBug();
}
}
