/* The script's cameras (0x452d00-0x453a70; docs/re/camera.md "The script's cameras"): the FSM's pushCam
   keeps the camera's state and takes the view (sub-mode 5, which Camera_SetMode refuses), each cam action
   then puts the camera somewhere for the frame, and popCam gives the view back. The path cameras move
   along one of the script's paths (an FSM arena record: +0x50 the count, +0x54 the points) and say when
   they reach its end (camIsArrived). The arguments are the script's words: hundredths of a metre and
   hundredths of a degree. */
#pragma once
#include "scene.h"
#include <stdint.h>

/* pushCam 0x452d00, popCam 0x452d60 */
void Cam_Push(void);
void Cam_Pop(void);
/* camObjDir 0x452d90: at the offset (x, y, z) in the object's frame, turned by (a, b, c) as Frame_Euler's
   angles */
void Cam_ObjDir(Node *o, int32_t x, int32_t y, int32_t z, int32_t a, int32_t c, int32_t b);
/* camObjObj 0x452f10: at the offset in o's frame, looking at the target */
void Cam_ObjObj(Node *o, int32_t x, int32_t y, int32_t z, Node *target);
/* camPosObj and camTransObj 0x453060: on the path at the height over the ground, moving along it at the
   speed (none: still), looking at the target */
void Cam_PathObj(uint32_t path, int32_t height, const int32_t *speed, Node *target);
/* camPosDir and camTransDir 0x453440: the same, turned by the angles */
void Cam_PathDir(uint32_t path, int32_t height, const int32_t *speed, int32_t a, int32_t c, int32_t b);
/* camF12 0x4537a0: up over a, back from it along the line to b (2 below b's place), tilted down when near */
void Cam_F12(Node *a, Node *b, int32_t back, int32_t up);
/* camIsArrived 0x453050: a path camera reached its path's end since the last ask */
int32_t Cam_IsArrived(void);
