/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __VERSION_H__
#define __VERSION_H__

// The version comes from the git tag, not from this file.
// version_from_git.py defines VERSION for a PlatformIO build, and the release
// workflow writes the tag here before it builds. This value is the fallback
// for a build with no release tag in reach.
#ifndef VERSION
#define VERSION "0.0.0"
#endif

#endif // __VERSION_H__
