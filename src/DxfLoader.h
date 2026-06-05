#pragma once

// Compatibilite ancienne API: le vrai code DXF est maintenant dans
// src/dxf_library/DxfLibrary.* compile comme bibliotheque separee.
#include "dxf_library/DxfLibrary.h"

using DxfLoader = DxfLibrary;
