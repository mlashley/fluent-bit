/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2019-2020 The Fluent Bit Authors
 *  Copyright (C) 2015-2018 Treasure Data Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <fluent-bit/flb_output_plugin.h>
#include <fluent-bit/flb_mem.h>
#include <fluent-bit/flb_utils.h>
#include <fluent-bit/flb_http_client.h>
#include <fluent-bit/flb_signv4.h>
#include <fluent-bit/flb_aws_credentials.h>
#include <fluent-bit/flb_upstream.h>
#include <fluent-bit/flb_upstream_ha.h>

#include "es.h"
#include "es_conf.h"

static flb_sds_t config_get_property(char *prop,
                                     struct flb_upstream_node *node,
                                     struct flb_elasticsearch *ctx)
{
    if (node) {
        return (flb_sds_t) flb_upstream_node_get_property(prop, node);
    }
    else {
        return (flb_sds_t) flb_output_get_property(prop, ctx->ins);
    }
}

static int config_set_properties(struct flb_upstream_node *node,
                                 struct flb_es_config *ec,
                                 struct flb_elasticsearch *ctx,
                                 struct flb_config *config)
{
    flb_sds_t tmp;
    const char *path;
#ifdef FLB_HAVE_AWS
    char *aws_role_arn = NULL;
    char *aws_external_id = NULL;
    char *aws_session_name = NULL;
#endif
    struct flb_uri *uri = ctx->ins->host.uri;
    struct flb_uri_field *f_index = NULL;
    struct flb_uri_field *f_type = NULL;

    if (uri) {
        if (uri->count >= 2) {
            f_index = flb_uri_get(uri, 0);
            f_type  = flb_uri_get(uri, 1);
        }
    }

    tmp = config_get_property("index", node, ctx);
    if (tmp) {
        ec->index = tmp;
    }
    else {
        ec->index = FLB_ES_DEFAULT_INDEX;
    }

    tmp = config_get_property("type", node, ctx);
    if (tmp) {
        ec->type = tmp;
    }
    else {
        ec->type = FLB_ES_DEFAULT_TYPE;
    }

    /* Set manual Index and Type */
    if (f_index) {
        ec->index = flb_strdup(f_index->value); /* FIXME */
    }

    if (f_type) {
        ec->type = flb_strdup(f_type->value); /* FIXME */
    }

    tmp = config_get_property("http_user", node, ctx);
    if (tmp) {
        ec->http_user = tmp;
    }
    else {
        ec->http_user = NULL;
    }

    tmp = config_get_property("http_passwd", node, ctx);
    if (tmp) {
        ec->http_passwd = tmp;
    }
    else {
        ec->http_passwd = "";
    }

    tmp = config_get_property("logstash_format", node, ctx);
    if (tmp) {
        ec->logstash_format = flb_utils_bool(tmp);
    }
    else {
        ec->logstash_format = FLB_FALSE;
    }

    tmp = config_get_property("logstash_prefix", node, ctx);
    if (tmp) {
        ec->logstash_prefix = flb_sds_create(tmp);
    }
    else {
        ec->logstash_prefix = flb_sds_create(FLB_ES_DEFAULT_PREFIX);
    }

    tmp = config_get_property("logstash_prefix_key", node, ctx);
    if (tmp) {
        ec->logstash_prefix_key = flb_sds_create(tmp);
    }
    else {
        ec->logstash_prefix_key = flb_sds_create("");
    }

    tmp = config_get_property("logstash_dateformat", node, ctx);
    if (tmp) {
        ec->logstash_dateformat = flb_sds_create(tmp);
    }
    else {
        ec->logstash_dateformat = flb_sds_create(FLB_ES_DEFAULT_TIME_FMT);
    }

    tmp = config_get_property("time_key", node, ctx);
    if (tmp) {
        ec->time_key = flb_sds_create(tmp);
    }
    else {
        ec->time_key = flb_sds_create("FLB_ES_DEFAULT_TIME_KEY");
    }

    tmp = config_get_property("time_key_format", node, ctx);
    if (tmp) {
        ec->time_key_format = flb_sds_create(tmp);
    }
    else {
        ec->time_key_format = flb_sds_create(FLB_ES_DEFAULT_TIME_KEYF);
    }

    tmp = config_get_property("include_tag_key", node, ctx);
    if (tmp) {
        ec->include_tag_key = flb_utils_bool(tmp);
    }
    else {
        ec->include_tag_key = FLB_FALSE;
    }

    tmp = config_get_property("tag_key", node, ctx);
    if (tmp) {
        ec->tag_key = flb_sds_create(tmp);
    }
    else {
        ec->tag_key = flb_sds_create(FLB_ES_DEFAULT_TAG_KEY);
    }

    tmp = config_get_property("buffer_size", node, ctx);
    if (tmp) {
        ec->buffer_size = flb_utils_size_to_bytes(tmp);
    }
    else {
        ec->buffer_size = flb_utils_size_to_bytes(FLB_ES_DEFAULT_HTTP_MAX);
    }

    /* HTTP Payload (response) maximum buffer size (0 == unlimited) */
    if (ec->buffer_size == -1) {
        ec->buffer_size = 0;
    }

    tmp = config_get_property("path", node, ctx);
    if (tmp) {
        path = tmp;
    }
    else {
        path = "";
    }

    tmp = config_get_property("pipeline", node, ctx);
    if (tmp) {
        snprintf(ec->uri, sizeof(ec->uri) - 1, "%s/_bulk/?pipeline=%s", path, tmp);
    }
    else {
        snprintf(ec->uri, sizeof(ec->uri) - 1, "%s/_bulk", path);
    }

    tmp = config_get_property("generate_id", node, ctx);
    if (tmp) {
        ec->generate_id = flb_utils_bool(tmp);
    }
    else {
        ec->generate_id = FLB_FALSE;
    }

    tmp = config_get_property("replace_dots", node, ctx);
    if (tmp) {
        ec->replace_dots = flb_utils_bool(tmp);
    }
    else {
        ec->replace_dots = FLB_FALSE;
    }

    tmp = config_get_property("current_time_index", node, ctx);
    if (tmp) {
        ec->current_time_index = flb_utils_bool(tmp);
    }
    else {
        ec->current_time_index = FLB_FALSE;
    }

    tmp = config_get_property("trace_output", node, ctx);
    if (tmp) {
        ec->trace_output = flb_utils_bool(tmp);
    }
    else {
        ec->trace_output = FLB_FALSE;
    }

    tmp = config_get_property("trace_error", node, ctx);
    if (tmp) {
        ec->trace_error = flb_utils_bool(tmp);
    }
    else {
        ec->trace_error = FLB_FALSE;
    }

    #ifdef FLB_HAVE_AWS
        /* AWS Auth */
        ec->has_aws_auth = FLB_FALSE;
        tmp = config_get_property("aws_auth", node, ctx);
        if (tmp) {
            if (strncasecmp(tmp, "On", 2) == 0) {
                ec->has_aws_auth = FLB_TRUE;
                flb_debug("[out_es] Enabled AWS Auth");

                /* AWS provider needs a separate TLS instance */
                ec->aws_tls.context = flb_tls_context_new(FLB_TRUE,
                                                           ctx->ins->tls_debug,
                                                           ctx->ins->tls_vhost,
                                                           ctx->ins->tls_ca_path,
                                                           ctx->ins->tls_ca_file,
                                                           ctx->ins->tls_crt_file,
                                                           ctx->ins->tls_key_file,
                                                           ctx->ins->tls_key_passwd);
                if (!ec->aws_tls.context) {
                    flb_errno();
                    elasticsearch_config_destroy(ec);
                    return -1;
                }

                tmp = config_get_property("aws_region", node, ctx);
                if (!tmp) {
                    flb_error("[out_es] aws_auth enabled but aws_region not set");
                    elasticsearch_config_destroy(ec);
                    return -1;
                }
                ec->aws_region = tmp;

                ec->aws_provider = flb_standard_chain_provider_create(config,
                                                                       &ec->aws_tls,
                                                                       ec->aws_region,
                                                                       NULL,
                                                                       flb_aws_client_generator());
                if (!ec->aws_provider) {
                    flb_error("[out_es] Failed to create AWS Credential Provider");
                    elasticsearch_config_destroy(ec);
                    return -1;
                }

                tmp = config_get_property("aws_role_arn", node, ctx);
                if (tmp) {
                    /* Use the STS Provider */
                    ec->base_aws_provider = ec->aws_provider;
                    aws_role_arn = tmp;
                    aws_external_id = NULL;
                    tmp = config_get_property("aws_external_id", node, ctx);
                    if (tmp) {
                        aws_external_id = tmp;
                    }

                    aws_session_name = flb_sts_session_name();
                    if (!aws_session_name) {
                        flb_error("[out_es] Failed to create aws iam role "
                                  "session name");
                        elasticsearch_config_destroy(ec);
                        return -1;
                    }

                    /* STS provider needs yet another separate TLS instance */
                    ec->aws_sts_tls.context = flb_tls_context_new(FLB_TRUE,
                                                                   ctx->ins->tls_debug,
                                                                   ctx->ins->tls_vhost,
                                                                   ctx->ins->tls_ca_path,
                                                                   ctx->ins->tls_ca_file,
                                                                   ctx->ins->tls_crt_file,
                                                                   ctx->ins->tls_key_file,
                                                                   ctx->ins->tls_key_passwd);
                    if (!ec->aws_sts_tls.context) {
                        flb_errno();
                        elasticsearch_config_destroy(ec);
                        return -1;
                    }

                    ec->aws_provider = flb_sts_provider_create(config,
                                                                &ec->aws_sts_tls,
                                                                ec->
                                                                base_aws_provider,
                                                                aws_external_id,
                                                                aws_role_arn,
                                                                aws_session_name,
                                                                ec->aws_region,
                                                                NULL,
                                                                flb_aws_client_generator());
                    /* Session name can be freed once provider is created */
                    flb_free(aws_session_name);
                    if (!ec->aws_provider) {
                        flb_error("[out_es] Failed to create AWS STS Credential "
                                  "Provider");
                        elasticsearch_config_destroy(ec);
                        return -1;
                    }

                }

                /* initialize credentials in sync mode */
                ec->aws_provider->provider_vtable->sync(ec->aws_provider);
                ec->aws_provider->provider_vtable->init(ec->aws_provider);
                /* set back to async */
                ec->aws_provider->provider_vtable->async(ec->aws_provider);
            }
        }
    #endif
    return 0;
}


static int elasticsearch_config_init(struct flb_es_config *ec,
                                      struct flb_elasticsearch *ctx)
{
    mk_list_add(&ec->_head, &ctx->configs);
    return 0;
}

void elasticsearch_config_destroy(struct flb_es_config *ec)
{

    flb_sds_destroy(ec->logstash_prefix);
    flb_sds_destroy(ec->logstash_prefix_key);
    flb_sds_destroy(ec->logstash_dateformat);
    flb_sds_destroy(ec->time_key);
    flb_sds_destroy(ec->time_key_format);
    flb_sds_destroy(ec->tag_key);

#ifdef FLB_HAVE_AWS
    if (ec->base_aws_provider) {
        flb_aws_provider_destroy(ec->base_aws_provider);
    }

    if (ec->aws_provider) {
        flb_aws_provider_destroy(ec->aws_provider);
    }

    if (ec->aws_tls.context) {
        flb_tls_context_destroy(ec->aws_tls.context);
    }

    if (ec->aws_sts_tls.context) {
        flb_tls_context_destroy(ec->aws_sts_tls.context);
    }
#endif

    flb_free(ec);
}



int elasticsearch_config_simple(struct flb_elasticsearch *ctx,
                                 struct flb_output_instance *ins,
                                 struct flb_config *config)
{
    int ret;
    int io_flags;
    struct flb_es_config *ec = NULL;
    struct flb_upstream *upstream;

    /* Set default network configuration if not set */
    flb_output_net_default("127.0.0.1", 9200, ins);

    /* Configuration context */
    ec = flb_calloc(1, sizeof(struct flb_es_config));
    if (!ec) {
        flb_errno();
        return -1;
    }

    /* Set default values */
    ret = flb_output_config_map_set(ins, ec);
    if (ret == -1) {
        flb_free(ec);
        return -1;
    }

    /* use TLS ? */
    if (ins->use_tls == FLB_TRUE) {
        io_flags = FLB_IO_TLS;
    }
    else {
        io_flags = FLB_IO_TCP;
    }

    if (ins->host.ipv6 == FLB_TRUE) {
        io_flags |= FLB_IO_IPV6;
    }

    /* Prepare an upstream handler */
    upstream = flb_upstream_create(config,
                                   ins->host.name,
                                   ins->host.port,
                                   io_flags, (void *) &ins->tls);
    if (!upstream) {
        flb_free(ec);
        flb_free(ctx);
        return -1;
    }
    ctx->u = upstream;
    flb_output_upstream_set(ctx->u, ins);

    /* Read properties into 'ec' context */
    config_set_properties(NULL, ec, ctx,config);

    /* Initialize and validate elasticsearch_config context */
    ret = elasticsearch_config_init(ec, ctx);
    if (ret == -1) {
        if (ec) {
            elasticsearch_config_destroy(ec);
        }
        return -1;
    }
    flb_plg_debug(ctx->ins, "host=%s port=%i uri=%s index=%s type=%s",
                  ins->host.name, ins->host.port, ec->uri,
                  ec->index, ec->type);

    return 0;
}
/* Configure in HA mode */
int elasticsearch_config_ha(const char *upstream_file,
                             struct flb_elasticsearch *ctx,
                             struct flb_config *config)
{
    int ret;
    struct mk_list *head;
    struct flb_upstream_node *node;
    struct flb_es_config *ec = NULL;

    ctx->ha_mode = FLB_TRUE;
    ctx->ha = flb_upstream_ha_from_file(upstream_file, config);
    if (!ctx->ha) {
        flb_plg_error(ctx->ins, "cannot load Upstream file");
        return -1;
    }

    /* Iterate nodes and create a elasticsearch_config context */
    mk_list_foreach(head, &ctx->ha->nodes) {
        node = mk_list_entry(head, struct flb_upstream_node, _head);

        /* create elasticsearch_config context */
        ec = flb_calloc(1, sizeof(struct flb_es_config));
        if (!ec) {
            flb_errno();
            flb_plg_error(ctx->ins, "failed config allocation");
            continue;
        }

        /* Read properties into 'ec' context */
        config_set_properties(node, ec, ctx,config);

        /* Initialize and validate elasticsearch_config context */
        ret = elasticsearch_config_init(ec, ctx);
        if (ret == -1) {
            if (ec) {
                elasticsearch_config_destroy(ec);
            }
            return -1;
        }

        /* Set our elasticsearch_config context into the node */
        flb_upstream_node_set_data(ec, node);


        flb_plg_debug(ctx->ins, "node=%s host=%s port=%s uri=%s index=%s type=%s",
                      node->name, node->host, node->port, ec->uri,
                      ec->index, ec->type);

    }

    return 0;
}
