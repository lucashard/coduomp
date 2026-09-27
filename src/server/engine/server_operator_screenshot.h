#ifndef SHARED_SERVER_OPERATOR_SCREENSHOT_H
#define SHARED_SERVER_OPERATOR_SCREENSHOT_H

/* NOT_FROM_ORIGINAL_SOURCE (whole file): master-branch remote-screenshot
 * anti-cheat feature. See server_operator_screenshot.c. */

#ifdef __cplusplus
extern "C" {
#endif

void coduomp_RemoteScreenshot_RegisterCvars(void);
void SV_RequestScreenshot_f(void);
void coduomp_RemoteScreenshot_AutoCaptureFrame(void);

#ifdef __cplusplus
}
#endif

#endif
