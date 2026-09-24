#pragma once

#include "esp_err.h"

// Development console (PROGRAMMING mode). Registers the `test` command family and starts the
// REPL; returns immediately (the REPL runs in its own task). Nothing runs until a command is
// typed - the 120 s scheduler is never auto-started from here.
esp_err_t devtest_start_console();

// Called at boot. If a development deep-sleep test was in progress (RTC-retained context),
// reports the wake and continues/finishes that test. Returns true if a test context was handled
// (the caller should then start the console instead of the normal NORMAL-mode scheduler).
bool devtest_resume_after_wake();

// Compact configuration status (CONFIG_NOT_SET items), also printed at boot.
void devtest_print_config_status();
