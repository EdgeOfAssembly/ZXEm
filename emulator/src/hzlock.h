/**
 * @file hzlock.h
 * @brief Save the X11 output refresh rate, switch to PAL 50 Hz, restore on quit.
 */
#pragma once

/**
 * @brief If a same-resolution ~50 Hz RandR mode exists, switch to it.
 * @return true if the mode was changed (restore will revert it).
 */
bool hz_lock_pal50();

/** @brief Restore the saved RandR mode. Safe to call more than once. */
void hz_lock_restore();
