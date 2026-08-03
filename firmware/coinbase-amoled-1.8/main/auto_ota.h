#pragma once

// FreeRTOS task entry. V2 performs authenticated HTTPS manifest checks and
// inactive-slot updates; V1 compiles the same source but deliberately disables
// automatic OTA because this release channel is V2-only.
void auto_ota_task(void*);
