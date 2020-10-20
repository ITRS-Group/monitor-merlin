#include "shared.h"
#include "module.h"
#include "logging.h"
#include <naemon/naemon.h>
#include <string.h>
#include "runcmd.h"
#include "queries.h"
#include "ipc.h"
#include "codec.h"

void send_runcmd_cmd(struct nm_event_execution_properties *evprop) {
	runcmd_ctx * ctx = (runcmd_ctx *) evprop->user_data;
	merlin_event pkt;
	merlin_node * node = ctx->node;

	memset(&pkt, 0, sizeof(merlin_event));
	memset(&pkt.hdr, 0, HDR_SIZE);

	pkt.hdr.sig.id = MERLIN_SIGNATURE;
	pkt.hdr.protocol = MERLIN_PROTOCOL_VERSION;
	gettimeofday(&pkt.hdr.sent, NULL);
	pkt.hdr.type = RUNCMD_PACKET;
	pkt.hdr.code = ctx->type;
	pkt.hdr.len = merlin_encode_event(&pkt, (void *)ctx->runcmd);

	if (pkt.hdr.len > sizeof(pkt.body)) {
		lerr("RUNCMD: Attempted to send %u bytes of data when max is %u",
			 pkt.hdr.len, sizeof(pkt.body));
		free(ctx->runcmd);
		free(ctx);
		return;
	}

	node_send(node, &pkt, packet_size(&pkt), MSG_DONTWAIT);
	free(ctx->runcmd);
	free(ctx);
}

void runcmd_wproc_callback(wproc_result *wpres, void * arg, int flags) {
	runcmd_ctx * ctx = (runcmd_ctx *) arg;
	struct nm_event_execution_properties evprop;
	/* Overwrite the ctx with response values */
	ctx->type = RUNCMD_RESP;
	if (wpres != NULL) {
		char * buf = kvvec_fetch_str_str(wpres->response, "outstd");
		ctx->runcmd->content = buf;
	} else {
		ctx->runcmd->content = strdup("Failed to get command");
	}
	evprop.user_data = (void *) ctx;
	send_runcmd_cmd(&evprop);
}

int handle_runcmd_event(merlin_node *node, merlin_event *pkt) {
	if (pkt->hdr.code == RUNCMD_CMD) {
		/* Execute and return send RESP packet back */
		runcmd_ctx * ctx;
		merlin_runcmd * runcmd = (merlin_runcmd *) pkt->body;
		char * full_cmd;
		size_t full_cmd_sz;
		char * cmd_prefix = "/usr/bin/mon qh query --single '@runcmd run ";
		int ret;

		ldebug("RUNCMD: Got RUNCMD_CMD packet from: %s", node->name);

		/* Need to re-malloc these as they would otherwise be free'd later on */
		/* These will eventually be free'd in send_runcmd_cmd */
		ctx = malloc(sizeof(*ctx));
		if (ctx == NULL) {
			lerr("RUNCMD: Failed to malloc context");
			return 0;
		}
		ctx->runcmd = malloc(sizeof(merlin_runcmd));
		ctx->runcmd->content = strdup(runcmd->content);
		ctx->runcmd->sd = runcmd->sd;
		ctx->node = node;

		/* size of full command, plus one for additional end quote and one for null terminator */
		full_cmd_sz=strlen(cmd_prefix) + strlen(ctx->runcmd->content)+2;
		full_cmd = malloc(full_cmd_sz);
		if (full_cmd == NULL) {
			lerr("RUNCMD: failed to malloc full_cmd");
			free(ctx->runcmd);
			free(ctx);
			return 0;
		}
		ret = snprintf(full_cmd, full_cmd_sz, "%s%s'", cmd_prefix, ctx->runcmd->content);
		if (ret < 0) {
			lerr("RUNCMD: could not generate full command");
			free(ctx->runcmd);
			free(ctx);
			free(full_cmd);
			return 0;
		}
		ldebug("RUNCMD: Full QH query: \n%s", full_cmd);
		wproc_run_callback(full_cmd, 5, runcmd_wproc_callback, (void*)ctx, 0);
		free(full_cmd);
		return 0;
	} else if (pkt->hdr.code == RUNCMD_RESP) {
		/* Callback to query handler */
		ldebug("RUNCMD: Got RUNCMD_RESP packet from: %s", node->name);
		return runcmd_callback(node, pkt);
	}
	else {
		/* Unknown code? */
		lwarn("RUNCMD: Got unkown RUNCMD type");
		return 0;
	}
}
