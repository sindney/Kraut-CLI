#pragma once

#include <KrautFoundation/Defines.h>

#if defined(AE_COMPILE_ENGINE_AS_DLL) && defined(_MSC_VER)
#  ifdef BUILDSYSTEM_BUILDING_KRAUTGENERATOR_LIB
#    define KRAUT_DLL __declspec(dllexport)
#  else
#    define KRAUT_DLL __declspec(dllimport)
#  endif
#else
#  define KRAUT_DLL
#endif
