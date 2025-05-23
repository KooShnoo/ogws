#include "egg/core/eggAllocator.h"
#include "egg/core/eggDisplay.h"
#include "egg/core/eggExpHeap.h"
#include "egg/core/eggGraphicsFifo.h"
#include "egg/core/eggHeap.h"
#include "egg/core/eggSystem.h"
#include "egg/core/eggVideo.h"
#include "egg/core/eggXfb.h"
#include "egg/core/eggXfbManager.h"
#include "egg/math/eggVector.h"
#include "g3d/g3d_camera.h"
#include "g3d/g3d_scnobj.h"
#include "macros.h"
#include "math/math_types.h"
#include "nw4r/g3d/g3d_init.h"
#include "nw4r/g3d/g3d_scnmdlsmpl.h"
#include "nw4r/g3d/g3d_scnroot.h"
#include "nw4r/g3d/g3d_state.h"
#include "nw4r/g3d/res/g3d_resfile.h"
#include "nw4r/g3d/res/g3d_resmdl.h"
#include "nw4r/ut/ut_Color.h"
#include "revolution/GX/GXTransform.h"
#include "revolution/GX/GXTypes.h"
#include "revolution/KPAD/KPAD.h"
#include "revolution/MTX/mtx44.h"
#include "revolution/MTX/mtxtypes.h"
#include "revolution/OS/OSArena.h"
#include "revolution/OS/OSError.h"
#include "revolution/OS/OSReset.h"
#include "revolution/SC/scapi.h"
#include "revolution/WPAD/WPAD.h"

#define HBM_APP_TYPE HBM_APP_TYPE_DVD
#include "revolution/HBM/HBMCommon.h"

// wip
static EGG::Allocator *s_WPADAllocator = nullptr;
bool s_did_request_shutdown = false;
bool s_did_request_reset = false;
bool s_did_request_wii_menu = false;
bool s_home_button_menu = false;
static KPADStatus s_controller_status = {};

static HBMDataInfo s_hbm_data_info;
static char s_hbm_config[] = "th_HomeBtn_b.brlyt,th_HomeBtn_b,1,1";
static HBMControllerData s_hbm_controller_data = {{
    {&s_controller_status, {0.0f, 0.0f}, WPAD_DEV_NONE},
    {nullptr, {0.0f, 0.0f}, WPAD_DEV_NONE},
    {nullptr, {0.0f, 0.0f}, WPAD_DEV_NONE},
    {nullptr, {0.0f, 0.0f}, WPAD_DEV_NONE},
}};

class InputManager {
public:
    static void *WPADAllocator(u32 size) { return s_WPADAllocator->alloc(size); }
    static int WPADFree(void *block) {
        s_WPADAllocator->free(block);
        return 1;
    }

    static bool isConnected() { return s_controller_status.dev_type < WPAD_DEV_NONE; }

    static void init(EGG::Allocator *pWPADAllocator) {

        s_WPADAllocator = pWPADAllocator;

        WPADRegisterAllocator(&WPADAllocator, &WPADFree);
        KPADInit();

        // i cheated to write this code.
        while (WPADGetStatus() != WPAD_LIB_STATUS_3) {}
    }

    static void calc() {
        KPADRead(0, &s_controller_status, 1);
        s_hbm_controller_data.wiiCon[0].use_devtype = s_controller_status.dev_type;
        s_hbm_controller_data.wiiCon[0].pos = s_controller_status.pos;
    }

    static inline bool pressedButton(s32 button) { return s_controller_status.hold & button; }
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
extern u8 _binary_bowling_pin_brres_start[];
extern u8 _binary_bowling_pin_brres_end[];
void *bowling_pin_rres = &_binary_bowling_pin_brres_start;

// dtk vfs cp orig/RSPE01_01/wsprts.wbfs:files/US/SportsStatic/local.carc:/HomeMenu/homeBtn_ENG.arc homeBtn_ENG.arc
// powerpc-eabi-objcopy homeBtn_ENG.arc homeBtn_ENG.o -I binary -O elf32-powerpc
extern u8 _binary_homeBtn_ENG_arc_start[];
extern u8 _binary_homeBtn_ENG_arc_end[];
void *home_button_menu_archive = &_binary_homeBtn_ENG_arc_start;

// dtk vfs cp orig/RSPE01_01/wsprts.wbfs:files/Common/SportsStatic/common.carc:/HomeMenu/SpeakerSe.arc SpeakerSe.arc
// powerpc-eabi-objcopy SpeakerSe.arc SpeakerSe.o -I binary -O elf32-powerpc
extern u8 _binary_SpeakerSe_arc_start[];
extern u8 _binary_SpeakerSe_arc_end[];
void *home_button_speaker_archive = &_binary_SpeakerSe_arc_start;

// dtk vfs cp orig/RSPE01_01/wsprts.wbfs:files/Common/SportsStatic/common.carc:/HomeMenu/home_nosave.csv home_nosave.txt
// powerpc-eabi-objcopy home_nosave.txt home_nosave.o-I binary -O elf32-powerpc
extern u8 _binary_home_nosave_txt_start[];
extern u8 _binary_home_nosave_txt_end[];
void *home_button_msgs = &_binary_home_nosave_txt_start;

// dtk vfs cp orig/RSPE01_01/wsprts.wbfs:files/Common/SportsStatic/common.carc:/HomeMenu/HomeButtonSe.brsar HomeButtonSe.brsar
// powerpc-eabi-objcopy HomeButtonSe.brsar HomeButtonSe.o -I binary -O elf32-powerpc
extern u8 _binary_HomeButtonSe_brsar_start[];
extern u8 _binary_HomeButtonSe_brsar_end[];
void *home_button_sound_archive = &_binary_HomeButtonSe_brsar_start;

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

    // HBMInit() initializes the menu itself, this initilizes the hbm library using HBMCreate().
    void init_hbm(EGG::Allocator *allocator) {
            s_hbm_data_info.layoutBuf =      home_button_menu_archive;
            s_hbm_data_info.spkSeBuf =       home_button_speaker_archive;
            s_hbm_data_info.msgBuf =         home_button_msgs;
            s_hbm_data_info.configBuf =      s_hbm_config;
            s_hbm_data_info.mem =            nullptr;
            s_hbm_data_info.sound_callback = nullptr; // todo: implement sound
            s_hbm_data_info.backFlag =       false;
            s_hbm_data_info.region =         SCGetLanguage();
            s_hbm_data_info.cursor =         0;
            s_hbm_data_info.messageFlag =    -1;
            s_hbm_data_info.configBufSize =  sizeof(s_hbm_config);
            s_hbm_data_info.memSize =        -1;
            s_hbm_data_info.frameDelta =     1.0f; // 60fps              
            s_hbm_data_info.adjust =         (Vec2) {832.0f/608.0f, 1.0f}; // 16:9 aspect ratio  
            s_hbm_data_info.pAllocator =     allocator;
        HBMCreate(&s_hbm_data_info);
        // HBMCreateSound is unused in wii sports. i have to either copy/paste it or write my own sound logic.
        // void*soundbuf = new u8[0x18700];
        // HBMCreateSound(home_button_sound_archive, soundbuf, 0x18700);
    }

    void calc_hbm() {
        HBMSelectBtnNum btn = HBMCalc(&s_hbm_controller_data);

        if (btn == HBM_SELECT_HOMEBTN) {
            s_home_button_menu = false;
            nw4r::g3d::G3dReset();
        } else if (btn == HBM_SELECT_WII_MENU) {
            s_did_request_wii_menu = true;
        } else if (btn == HBM_SELECT_BTN_RESET) {
            s_did_request_reset = true;
        }

        nw4r::math::MTX44 projection_matrix;
        // magic numbers pilfered from mario kart wii. i ain't doin no mathematics
        C_MTXOrtho(projection_matrix, 228.0f, -228.0f, -304.0f, 304.0f, 0.0f, 500.0f);
        GXSetProjection(projection_matrix, GX_ORTHOGRAPHIC);
    }

    void slugBug() {
        EGG::BaseSystem::mConfigData = this;

        init_heap();
        EGG::Allocator allocator = EGG::Allocator(m_heap, 32);
        EGG::Allocator mem2_allocator = EGG::Allocator(m_mem2_heap, 32);

        // locks up on console :/ idk why, dolphin likes it fine
        // OSSetPowerCallback(&onPowerButton);
        // OSSetResetCallback(&onResetButton);

        InputManager::init(&mem2_allocator);
        init_hbm(&allocator);

        EGG::GraphicsFifo::create(gpu_fifo_size, nullptr);

        m_video = new EGG::Video(nullptr);

        m_xfb_mgr = new EGG::XfbManager(nullptr);
        m_xfb_mgr->attach(new EGG::Xfb(nullptr));
        m_xfb_mgr->attach(new EGG::Xfb(nullptr));

        m_display = new EGG::Display(1);
        m_display->setBlack(true);

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

        m_display->setClearColor(nw4r::ut::Color(0x1e1e1eff));

        OSReport("entering main loop.\n");
        while (true) {
            m_display->beginFrame();
            m_display->beginRender();

            if (s_did_request_shutdown) { OSShutdownSystem(); }
            // OSRestart is missing for some reason
            if (s_did_request_reset)    { OSReturnToMenu();   }
            if (s_did_request_wii_menu) { OSReturnToMenu();   }

            InputManager::calc();

            if (InputManager::pressedButton(WPAD_BUTTON_RIGHT)) { pos.x += 0.1f; }
            if (InputManager::pressedButton(WPAD_BUTTON_PLUS))  { pos.y += 0.1f; }
            if (InputManager::pressedButton(WPAD_BUTTON_UP))    { pos.z += 0.1f; }
            if (InputManager::pressedButton(WPAD_BUTTON_LEFT))  { pos.x -= 0.1f; }
            if (InputManager::pressedButton(WPAD_BUTTON_MINUS)) { pos.y -= 0.1f; }
            if (InputManager::pressedButton(WPAD_BUTTON_DOWN))  { pos.z -= 0.1f; }
            MTX34SetPos(model_matrix, pos);
            scene_pin->SetMtx(nw4r::g3d::ScnObj::MTX_LOCAL, &model_matrix);

            if (!s_home_button_menu && InputManager::pressedButton(WPAD_BUTTON_HOME)) { 
                s_home_button_menu = true;
                HBMInit();
            }

            if (s_home_button_menu) {
                calc_hbm();
                HBMDraw();
            } else {
                // calc
                scene_3d->CalcWorld();
                scene_3d->CalcMaterial();
                scene_3d->CalcView();
                scene_3d->GatherDrawScnObj();
                scene_3d->ZSort();

                // draw
                scene_3d->DrawOpa();
                scene_3d->DrawXlu();
            }

            m_display->endRender();
            m_display->endFrame();
        }
    }
};

extern "C" {
void slugBug() {
    SlugBugSystem slug_bug_system = SlugBugSystem();
    slug_bug_system.slugBug();
}
}
