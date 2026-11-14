#pragma once
#include "pad.h"
@class GCController;
// One immutable framework snapshot, mapped to the console's physical layout.
PadSample sampleAppleController(GCController *controller);
