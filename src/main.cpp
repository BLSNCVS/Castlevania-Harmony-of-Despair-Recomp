// hodrecomp - Castlevania: Harmony of Despair (Xbox 360) static recompilation

#include "generated/default/hodrecomp_init.h"

#include "hodrecomp_app.h"

REXCVAR_DEFINE_BOOL(hod_headless, false, "HoD",
                    "Run without graphics, for development: nothing is drawn")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_BOOL(hod_installer, false, "HoD",
                    "Show the installer at startup, e.g. to add DLC to an existing install")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REX_DEFINE_APP(hodrecomp, HodrecompApp::Create)
