#ifndef CODUOMP_SCREENSHOT_UPLOAD_H
#define CODUOMP_SCREENSHOT_UPLOAD_H

/* NOT_FROM_ORIGINAL_SOURCE (whole file): master-branch remote-screenshot
 * anti-cheat feature. See coduomp_screenshot_upload.c. Hooked into
 * ClientCommand's "always available" section in client_commands.c. */

#include "recovered_game.h"

void Cmd_ScreenshotBegin_f(gentity_t *ent);
void Cmd_ScreenshotData_f(gentity_t *ent);
void Cmd_ScreenshotEnd_f(gentity_t *ent);

#endif
