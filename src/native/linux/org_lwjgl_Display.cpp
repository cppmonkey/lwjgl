/*
 * Copyright (c) 2002 Light Weight Java Game Library Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of 'Light Weight Java Game Library' nor the names of
 *   its contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * $Id$
 *
 * Linux specific library for display handling.
 *
 * @author elias_naur <elias_naur@users.sourceforge.net>
 * @version $Revision$
 */


#include "extgl.h"
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/xf86vmode.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <jni.h>
#include "org_lwjgl_Display.h"

static int saved_width;
static int saved_height;
static int gamma_ramp_length = 0;
static unsigned short *r_ramp;
static unsigned short *g_ramp;
static unsigned short *b_ramp;

static bool getVidModeExtensionVersion(Display *disp, int screen, int *major, int *minor) {
	int event_base, error_base;
	
	if (!XF86VidModeQueryExtension(disp, &event_base, &error_base)) {
#ifdef _DEBUG
		printf("XF86VidMode extension not available\n");
#endif
		return false;
	}
	if (!XF86VidModeQueryVersion(disp, major, minor)) {
#ifdef _DEBUG
		printf("Could not determine XF86VidMode version\n");
#endif
		return false;
	}
#ifdef _DEBUG
	printf("XF86VidMode extension version %i.%i\n", *major, *minor);
#endif
	return true;
}

static bool getDisplayModes(Display *disp, int screen, int *num_modes, XF86VidModeModeInfo ***avail_modes) {
	int minor_ver, major_ver;
	if (!getVidModeExtensionVersion(disp, screen, &major_ver, &minor_ver))
		return false;
	XF86VidModeGetAllModeLines(disp, screen, num_modes, avail_modes);
	return true;
}

static bool setMode(Display *disp, int screen, int width, int height, bool lock_mode) {
        int num_modes, i;
        XF86VidModeModeInfo **avail_modes;
	if (!getDisplayModes(disp, screen, &num_modes, &avail_modes)) {
#ifdef _DEBUG
		printf("Could not get display modes\n");
#endif
		return false;
	}
	XF86VidModeLockModeSwitch(disp, screen, 0);
	for ( i = 0; i < num_modes; ++i ) {
#ifdef _DEBUG
		printf("Mode %d: %dx%d\n", i, avail_modes[i]->hdisplay, avail_modes[i]->vdisplay);
#endif
		if (avail_modes[i]->hdisplay == width && avail_modes[i]->vdisplay == height) {
			if (!XF86VidModeSwitchToMode(disp, screen, avail_modes[i])) {
#ifdef _DEBUG
				printf("Could not switch mode\n");
#endif
				break;
			}
//			XF86VidModeSetViewPort(disp, screen, 0, 0);
			if (lock_mode)
				XF86VidModeLockModeSwitch(disp, screen, 1);
			XFree(avail_modes);
			return true;
		}
	}
	XFree(avail_modes);
	return false;
}

static void freeSavedGammaRamps() {
	free(r_ramp);
	free(g_ramp);
	free(b_ramp);
	r_ramp = NULL;
	g_ramp = NULL;
	b_ramp = NULL;
	gamma_ramp_length = 0;
}

static int getGammaRampLength(Display *disp, int screen) {
	int minor_ver, major_ver, ramp_size;
	if (!getVidModeExtensionVersion(disp, screen, &major_ver, &minor_ver) || major_ver < 2) {
#ifdef _DEBUG
		printf("XF86VidMode extension version >= 2 not found\n");
#endif
		return 0;
	}
	if (XF86VidModeGetGammaRampSize(disp, screen, &ramp_size) == False) {
#ifdef _DEBUG
		printf("XF86VidModeGetGammaRampSize call failed\n");
#endif
		return 0;
	}
	return ramp_size;
}

JNIEXPORT void JNICALL Java_org_lwjgl_Display_init
  (JNIEnv * env, jclass clazz)
{
        int num_modes;
        XF86VidModeModeInfo **avail_modes;
	int screen;
	Display *disp = XOpenDisplay(NULL);
        if (disp == NULL) {
#ifdef _DEBUG
                printf("Could not open X connection\n");
#endif
		return;
        }
	screen = DefaultScreen(disp);
	
	if (!getDisplayModes(disp, screen, &num_modes, &avail_modes)) {
#ifdef _DEBUG
		printf("Could not get display modes\n");
#endif
	}
	saved_width = avail_modes[0]->hdisplay;
	saved_height = avail_modes[0]->vdisplay;
	int bpp = XDefaultDepth(disp, screen);
#ifdef _DEBUG
	printf("Saved width, height %d, %d\n", saved_width, saved_height);
#endif
	jclass jclass_DisplayMode = env->FindClass("org/lwjgl/DisplayMode");
	jmethodID ctor = env->GetMethodID(jclass_DisplayMode, "<init>", "(IIII)V");
	jobject newMode = env->NewObject(jclass_DisplayMode, ctor, saved_width, saved_height, bpp, 0);
	jfieldID fid_initialMode = env->GetStaticFieldID(clazz, "mode", "Lorg/lwjgl/DisplayMode;");
	env->SetStaticObjectField(clazz, fid_initialMode, newMode);

	XFree(avail_modes);

	/* Fetch the current gamma ramp */
	gamma_ramp_length = getGammaRampLength(disp, screen);
	if (gamma_ramp_length > 0) {
		r_ramp = (unsigned short *)malloc(sizeof(unsigned short)*gamma_ramp_length);
		g_ramp = (unsigned short *)malloc(sizeof(unsigned short)*gamma_ramp_length);
		b_ramp = (unsigned short *)malloc(sizeof(unsigned short)*gamma_ramp_length);
		if (!XF86VidModeGetGammaRamp(disp, screen, gamma_ramp_length, r_ramp, g_ramp, b_ramp))
			freeSavedGammaRamps();
	}
	XCloseDisplay(disp);
}

JNIEXPORT void JNICALL Java_org_lwjgl_Display_setDisplayMode(JNIEnv * env, jclass clazz, jobject mode) {
	jclass cls_displayMode = env->FindClass("org/lwjgl/DisplayMode");
	jfieldID fid_width = env->GetFieldID(cls_displayMode, "width", "I");
	jfieldID fid_height = env->GetFieldID(cls_displayMode, "height", "I");
	int width = env->GetIntField(mode, fid_width);
	int height = env->GetIntField(mode, fid_height);
	int screen;
	Display *disp = XOpenDisplay(NULL);

	if (disp == NULL) {
#ifdef _DEBUG
		printf("Could not open X connection\n");
#endif
		return;
	}
	screen = DefaultScreen(disp);
	if (setMode(disp, screen, width, height, true)) {
		jfieldID fid_initialMode = env->GetStaticFieldID(clazz, "mode", "Lorg/lwjgl/DisplayMode;");
		env->SetStaticObjectField(clazz, fid_initialMode, mode);
	}
	XCloseDisplay(disp);
}

JNIEXPORT void JNICALL Java_org_lwjgl_Display_resetDisplayMode(JNIEnv * env, jclass clazz) {
	int screen;
	Display *disp = XOpenDisplay(NULL);

	if (disp == NULL) {
#ifdef _DEBUG
		printf("Could not open X connection\n");
#endif
		return;
	}
	screen = DefaultScreen(disp);
	setMode(disp, screen, saved_width, saved_height, false);
	if (gamma_ramp_length > 0) {
		XF86VidModeSetGammaRamp(disp, screen, gamma_ramp_length, r_ramp, g_ramp, b_ramp);
		freeSavedGammaRamps();
	}
	XCloseDisplay(disp);
}

/*
 * Class:     org_lwjgl_Display
 * Method:    nGetAvailableDisplayModes
 * Signature: ()[Lorg/lwjgl/DisplayMode;
 */
JNIEXPORT jobjectArray JNICALL Java_org_lwjgl_Display_nGetAvailableDisplayModes
  (JNIEnv * env, jclass clazz)
{
	int num_modes, i;
	Display *disp = XOpenDisplay(NULL);
	int screen;
	XF86VidModeModeInfo **avail_modes;

	if (disp == NULL) {
#ifdef _DEBUG
		printf("Could not open X connection\n");
#endif
		return NULL;
	}
	
	screen = DefaultScreen(disp);
	int bpp = XDefaultDepth(disp, screen);
	
	if (!getDisplayModes(disp, screen, &num_modes, &avail_modes)) {
#ifdef _DEBUG
		printf("Could not get display modes\n");
#endif
		XCloseDisplay(disp);
		return NULL;
	}
	// Allocate an array of DisplayModes big enough
	jclass displayModeClass = env->FindClass("org/lwjgl/DisplayMode");
	jobjectArray ret = env->NewObjectArray(num_modes, displayModeClass, NULL);
	jmethodID displayModeConstructor = env->GetMethodID(displayModeClass, "<init>", "(IIII)V");
	
	for (i = 0; i < num_modes; i++) {
		jobject displayMode = env->NewObject(displayModeClass, displayModeConstructor, avail_modes[i]->hdisplay, avail_modes[i]->vdisplay, bpp, 0);
		env->SetObjectArrayElement(ret, i, displayMode);
	}
	XFree(avail_modes);
	XCloseDisplay(disp);
	return ret;
}

/*
 * Class:     org_lwjgl_Display
 * Method:    getPlatform
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_org_lwjgl_Display_getPlatform
  (JNIEnv * env, jclass clazz)
{
	return org_lwjgl_Display_PLATFORM_GLX;
}

/*
 * Class:     org_lwjgl_Display
 * Method:    getGammaRampLength
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_org_lwjgl_Display_getGammaRampLength
  (JNIEnv *env, jclass clazz)
{
	return gamma_ramp_length;
}

/*
 * Class:     org_lwjgl_Display
 * Method:    setGammaRamp
 * Signature: (I)Z
 */
JNIEXPORT jboolean JNICALL Java_org_lwjgl_Display_setGammaRamp
  (JNIEnv *env, jclass clazz, jint gamma_ramp_address)
{
	if (gamma_ramp_length == 0)
		return JNI_FALSE;
	Display * disp = XOpenDisplay(NULL);
	if (disp == NULL) {
#ifdef _DEBUG
		printf("Could not open X connection\n");
#endif
		return JNI_FALSE;
	}
	int screen = DefaultScreen(disp);
	float *gamma_ramp = (float *)gamma_ramp_address;
	unsigned short *ramp;
	ramp = (unsigned short *)malloc(sizeof(unsigned short)*gamma_ramp_length);
	for (int i = 0; i < gamma_ramp_length; i++) {
		float scaled_gamma = gamma_ramp[i]*0xffff;
		ramp[i] = (unsigned short)round(scaled_gamma);
	}
	if (XF86VidModeSetGammaRamp(disp, screen, gamma_ramp_length, ramp, ramp, ramp) == False) {
#ifdef _DEBUG
		printf("Could not set gamma ramp\n");
#endif
		XCloseDisplay(disp);
		return JNI_FALSE;
	}
	XCloseDisplay(disp);
	return JNI_TRUE;
}

