#include "egg/core/eggAllocator.h"
#include "egg/core/eggDisplay.h"
#include "egg/core/eggExpHeap.h"
#include "egg/core/eggGraphicsFifo.h"
#include "egg/core/eggHeap.h"
#include "egg/core/eggSystem.h"
#include "egg/core/eggVideo.h"
#include "egg/core/eggXfb.h"
#include "egg/core/eggXfbManager.h"
#include "nw4r/g3d/g3d_init.h"
#include "nw4r/g3d/g3d_scnmdlsmpl.h"
#include "nw4r/g3d/g3d_scnroot.h"
#include "nw4r/g3d/g3d_state.h"
#include "nw4r/g3d/res/g3d_resfile.h"
#include "nw4r/g3d/res/g3d_resmdl.h"
#include "revolution/OS/OSArena.h"
#include "revolution/OS/OSError.h"
#include "nw4r/ut/ut_Color.h"
#include "revolution/VI/vi.h"

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

    void init_heap() {
        void *heap_base = OSAllocFromMEM1ArenaLo(heap_size, 32);
        ASSERT(heap_base);

        EGG::Heap::initialize();
        EGG::ExpHeap *heap = EGG::ExpHeap::create(heap_base, heap_size, 0);
        heap->becomeCurrentHeap();
        m_heap = heap;
    }
    
    void slugBug() {
        EGG::BaseSystem::mConfigData = this;
        
        init_heap();
        EGG::Allocator allocator = EGG::Allocator(m_heap, 32);

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
        while (m_display->mFrameCount < 2 * 60) {
            m_display->beginFrame();
            m_display->beginRender();
            
            // r += 1;
            // g += 1;
            // b += 1;
            // m_display->setClearColor(nw4r::ut::Color(r, g, b, 0xff));

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
