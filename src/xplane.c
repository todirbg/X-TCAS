/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <XPLMDataAccess.h>
#include <XPLMDisplay.h>
#include <XPLMGraphics.h>
#include <XPLMPlanes.h>
#include <XPLMPlugin.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#include <acfutils/acfutils.h>
#include <acfutils/assert.h>
#include <acfutils/avl.h>
#include <acfutils/dr.h>
#include <acfutils/geom.h>
#include <acfutils/log.h>
#include <acfutils/helpers.h>
#include <acfutils/types.h>
#include <acfutils/thread.h>

#include "snd_sys.h"
#include "xtcas.h"
#include "xplane.h"

#define	FLOOP_INTVAL			0.1
#define	POS_UPDATE_INTVAL		0.1
#define	XTCAS_PLUGIN_NAME		"X-TCAS 1.0"
#define	XTCAS_PLUGIN_SIG		"skiselkov.xtcas.1.0"
#define	XTCAS_PLUGIN_DESCRIPTION \
	"Generic TCAS II v7.1 implementation for X-Plane"

#define	MAX_MP_PLANES	19
#define	MAX_DR_NAME_LEN	256

static bool_t intf_inited = B_FALSE;
static struct {
	dr_t	time;
	dr_t	baro_alt;
	dr_t	rad_alt;
	dr_t	lat;
	dr_t	lon;
	dr_t	plane_x;
	dr_t	plane_y;
	dr_t	plane_z;
	dr_t	view_is_ext;
	dr_t	warn_volume;
} drs;

static struct {
	dr_t	x;
	dr_t	y;
	dr_t	z;
} mp_planes[MAX_MP_PLANES];

static mutex_t acf_pos_lock;
static avl_tree_t acf_pos_tree;
static geo_pos3_t my_acf_pos;
static double my_acf_agl = 0;
static double last_pos_collected = 0;
static double cur_sim_time = 0;

static char plugindir[512] = { 0 };

static double xp_get_time(void *handle);
static void xp_get_my_acf_pos(void *handle, geo_pos3_t *pos, double *alt_agl);
static void xp_get_oth_acf_pos(void *handle, acf_pos_t **pos_p, size_t *num);

static const sim_intf_ops_t xp_intf_ops = {
	.handle = NULL,
	.get_time = xp_get_time,
	.get_my_acf_pos = xp_get_my_acf_pos,
	.get_oth_acf_pos = xp_get_oth_acf_pos,
	.update_contact = NULL,
	.update_RA = NULL,
	.update_RA_prediction = NULL
};

static int
acf_pos_compar(const void *a, const void *b)
{
	const acf_pos_t *pa = a, *pb = b;

	if (pa->acf_id < pb->acf_id)
		return (-1);
	else if (pa->acf_id == pb->acf_id)
		return (0);
	else
		return (1);
}

static void
sim_intf_init(void)
{
	fdr_find(&drs.time, "sim/time/total_running_time_sec");
	fdr_find(&drs.baro_alt, "sim/flightmodel/misc/h_ind");
	fdr_find(&drs.rad_alt,
	    "sim/cockpit2/gauges/indicators/radio_altimeter_height_ft_pilot");
	fdr_find(&drs.lat, "sim/flightmodel/position/latitude");
	fdr_find(&drs.lon, "sim/flightmodel/position/longitude");

	fdr_find(&drs.plane_x, "sim/flightmodel/position/local_x");
	fdr_find(&drs.plane_y, "sim/flightmodel/position/local_y");
	fdr_find(&drs.plane_z, "sim/flightmodel/position/local_z");

	fdr_find(&drs.view_is_ext, "sim/graphics/view/view_is_external");
	fdr_find(&drs.warn_volume, "sim/operation/sound/warning_volume_ratio");

	for (int i = 0; i < MAX_MP_PLANES; i++) {
		fdr_find(&mp_planes[i].x,
		    "sim/multiplayer/position/plane%d_x", i + 1);
		fdr_find(&mp_planes[i].y,
		    "sim/multiplayer/position/plane%d_y", i + 1);
		fdr_find(&mp_planes[i].z,
		    "sim/multiplayer/position/plane%d_z", i + 1);
	}

	avl_create(&acf_pos_tree, acf_pos_compar, sizeof (acf_pos_t),
	    offsetof(acf_pos_t, tree_node));
	mutex_init(&acf_pos_lock);

	intf_inited = B_TRUE;
}

static void
sim_intf_fini(void)
{
	void *cookie = NULL;
	acf_pos_t *p;

	memset(&drs, 0, sizeof (drs));
	memset(&mp_planes, 0, sizeof (mp_planes));

	while ((p = avl_destroy_nodes(&acf_pos_tree, &cookie)) != NULL)
		free(p);
	avl_destroy(&acf_pos_tree);
	mutex_destroy(&acf_pos_lock);

	intf_inited = B_FALSE;
}

static int
acf_pos_collector(XPLMDrawingPhase phase, int before, void *ref)
{
	UNUSED(phase);
	UNUSED(before);
	UNUSED(ref);

	double now;

	/* grab updates only at a set interval */
	now = xp_get_time(NULL);
	if (last_pos_collected + POS_UPDATE_INTVAL > now)
		return (1);
	last_pos_collected = now;

	/* grab our aircraft position */
	my_acf_pos.lat = dr_getf(&drs.lat);
	my_acf_pos.lon = dr_getf(&drs.lon);
	my_acf_pos.elev = dr_getf(&drs.baro_alt);
	my_acf_agl = dr_getf(&drs.rad_alt);

	/* grab all other aircraft positions */
	for (int i = 0; i < MAX_MP_PLANES; i++) {
		vect3_t local;
		geo_pos3_t world;
		avl_index_t where;
		acf_pos_t srch;
		acf_pos_t *pos;

		local.x = dr_getf(&mp_planes[i].x);
		local.y = dr_getf(&mp_planes[i].y);
		local.z = dr_getf(&mp_planes[i].z);

		mutex_enter(&acf_pos_lock);

		srch.acf_id = (void *)(uintptr_t)(i + 1);
		pos = avl_find(&acf_pos_tree, &srch, &where);
		/*
		 * This is exceedingly unlikely, so it's "good enough" to use
		 * as an emptiness test.
		 */
		if (IS_ZERO_VECT3(local)) {
			if (pos != NULL) {
				avl_remove(&acf_pos_tree, pos);
				free(pos);
			}
		} else {
			if (pos == NULL) {
				pos = calloc(1, sizeof (*pos));
				pos->acf_id = (void *)(uintptr_t)(i + 1);
				avl_insert(&acf_pos_tree, pos, where);
			}
			XPLMLocalToWorld(local.x, local.y, local.z,
			    &world.lat, &world.lon, &world.elev);
			pos->pos = world;
		}

		mutex_exit(&acf_pos_lock);
	}

	return (1);
}

/*
 * Called from X-TCAS to get the current simulator time.
 */
static double
xp_get_time(void *handle)
{
	UNUSED(handle);
	ASSERT(intf_inited);
	return (cur_sim_time);
}

/*
 * Called from X-TCAS to get our aircraft position.
 */
static void
xp_get_my_acf_pos(void *handle, geo_pos3_t *pos, double *alt_agl)
{
	UNUSED(handle);
	ASSERT(intf_inited);
	*pos = my_acf_pos;
	*alt_agl = my_acf_agl;
}

/*
 * Called from X-TCAS to gather intruder aircraft position.
 */
static void
xp_get_oth_acf_pos(void *handle, acf_pos_t **pos_p, size_t *num)
{
	size_t i;
	acf_pos_t *pos;

	UNUSED(handle);

	mutex_enter(&acf_pos_lock);
	*num = avl_numnodes(&acf_pos_tree);
	*pos_p = calloc(*num, sizeof (*pos));
	for (pos = avl_first(&acf_pos_tree), i = 0; pos != NULL;
	    pos = AVL_NEXT(&acf_pos_tree, pos), i++) {
		ASSERT3U(i, <, *num);
		memcpy(&(*pos_p)[i], pos, sizeof (*pos));
	}
	mutex_exit(&acf_pos_lock);
}

/*
 * Called by the plugin flight loop every simulator frame.
 */
static float
floop_cb(float elapsed_since_last_call, float elapsed_since_last_floop,
    int counter, void *refcon)
{
	double volume = (dr_geti(&drs.view_is_ext) != 1) ?
	    dr_getf(&drs.warn_volume) : 0;

	UNUSED(elapsed_since_last_call);
	UNUSED(elapsed_since_last_floop);
	UNUSED(counter);
	UNUSED(refcon);

	cur_sim_time = dr_getf(&drs.time);
	xtcas_run();
	xtcas_snd_sys_run(volume);

	return (-1.0);
}

PLUGIN_API int
XPluginStart(char *name, char *sig, char *desc)
{
	char *snd_dir;

	acfutils_logfunc = XPLMDebugString;

	XPLMGetPluginInfo(XPLMGetMyID(), NULL, plugindir, NULL, NULL);

	strcpy(name, XTCAS_PLUGIN_NAME);
	strcpy(sig, XTCAS_PLUGIN_SIG);
	strcpy(desc, XTCAS_PLUGIN_DESCRIPTION);
	sim_intf_init();
	snd_dir = mkpathname(plugindir, "data", "male1", NULL);
	if (!xtcas_snd_sys_init(plugindir)) {
		free(snd_dir);
		return (0);
	}
	free(snd_dir);

	return (1);
}

PLUGIN_API void
XPluginStop(void)
{
	xtcas_snd_sys_fini();
	sim_intf_fini();
}

PLUGIN_API void
XPluginEnable(void)
{
	xtcas_init(&xp_intf_ops);
	XPLMRegisterFlightLoopCallback(floop_cb, FLOOP_INTVAL, NULL);
	XPLMRegisterDrawCallback(acf_pos_collector, xplm_Phase_Panel, 1, NULL);
}

PLUGIN_API void
XPluginDisable(void)
{
	XPLMUnregisterDrawCallback(acf_pos_collector, xplm_Phase_Panel,
	    1, NULL);
	XPLMUnregisterFlightLoopCallback(floop_cb, NULL);
	xtcas_fini();
}

PLUGIN_API void
XPluginReceiveMessage(XPLMPluginID from, int msg, void *param)
{
	UNUSED(from);
	UNUSED(msg);
	UNUSED(param);
}
