#ifndef VERSION_H
#define VERSION_H

/* The version comes from the last version tag (vX.Y.Z), through inc/git.h,
   which tools/git-version.ps1 writes before each build */
#include "git.h"

#define VERSION_MAJOR    GIT_VERSION_MAJOR
#define VERSION_MINOR    GIT_VERSION_MINOR
#define VERSION_BUILD    GIT_VERSION_BUILD
#define VERSION_REVISION GIT_VERSION_REVISION

#define VERSION          VERSION_MAJOR, VERSION_MINOR, VERSION_BUILD, VERSION_REVISION
#define VERSION_STRING   GIT_VERSION_STRING

#endif
