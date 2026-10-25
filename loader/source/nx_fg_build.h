#pragma once
// The Unity/GLES port is the normal build. Keep the expensive deko experiment
// available as an explicit developer build, without linking it into releases.
#ifndef NX_EXPERIMENTAL_FRAMEGEN
#define NX_EXPERIMENTAL_FRAMEGEN 0
#endif
