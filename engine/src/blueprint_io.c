/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/blueprint.h>
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *pin_type_to_str(af_bp_pin_type type) {
    switch (type) {
    case AF_BP_PIN_EXEC:
        return "exec";
    case AF_BP_PIN_FLOAT:
        return "float";
    case AF_BP_PIN_BOOL:
        return "bool";
    case AF_BP_PIN_VEC3:
        return "vec3";
    case AF_BP_PIN_OBJECT:
        return "object";
    case AF_BP_PIN_STRING:
        return "string";
    default:
        return "unknown";
    }
}

static af_bp_pin_type pin_type_from_str(const char *str) {
    if (!str)
        return AF_BP_PIN_FLOAT;
    if (strcmp(str, "exec") == 0)
        return AF_BP_PIN_EXEC;
    if (strcmp(str, "float") == 0)
        return AF_BP_PIN_FLOAT;
    if (strcmp(str, "bool") == 0)
        return AF_BP_PIN_BOOL;
    if (strcmp(str, "vec3") == 0)
        return AF_BP_PIN_VEC3;
    if (strcmp(str, "object") == 0)
        return AF_BP_PIN_OBJECT;
    if (strcmp(str, "string") == 0)
        return AF_BP_PIN_STRING;
    return AF_BP_PIN_FLOAT;
}

static cJSON *val_to_json(af_bp_value val) {
    switch (val.type) {
    case AF_BP_PIN_FLOAT:
        return cJSON_CreateNumber((double)val.f);
    case AF_BP_PIN_BOOL:
        return cJSON_CreateBool(val.b);
    case AF_BP_PIN_VEC3: {
        cJSON *arr = cJSON_CreateArray();
        cJSON_AddItemToArray(arr, cJSON_CreateNumber((double)val.v.x));
        cJSON_AddItemToArray(arr, cJSON_CreateNumber((double)val.v.y));
        cJSON_AddItemToArray(arr, cJSON_CreateNumber((double)val.v.z));
        return arr;
    }
    case AF_BP_PIN_OBJECT:
        return cJSON_CreateNumber((double)val.obj);
    case AF_BP_PIN_STRING:
        return cJSON_CreateString(val.s);
    default:
        return cJSON_CreateNull();
    }
}

static af_bp_value val_from_json(const cJSON *j, af_bp_pin_type type) {
    af_bp_value v = {0};
    v.type = type;
    if (!j)
        return v;
    switch (type) {
    case AF_BP_PIN_FLOAT:
        if (cJSON_IsNumber(j))
            v.f = (float)j->valuedouble;
        break;
    case AF_BP_PIN_BOOL:
        if (cJSON_IsBool(j))
            v.b = cJSON_IsTrue(j);
        break;
    case AF_BP_PIN_VEC3:
        if (cJSON_IsArray(j) && cJSON_GetArraySize(j) >= 3) {
            v.v.x = (float)cJSON_GetArrayItem(j, 0)->valuedouble;
            v.v.y = (float)cJSON_GetArrayItem(j, 1)->valuedouble;
            v.v.z = (float)cJSON_GetArrayItem(j, 2)->valuedouble;
        }
        break;
    case AF_BP_PIN_OBJECT:
        if (cJSON_IsNumber(j))
            v.obj = (af_object_id)j->valuedouble;
        break;
    case AF_BP_PIN_STRING:
        if (cJSON_IsString(j))
            snprintf(v.s, sizeof(v.s), "%s", j->valuestring);
        break;
    default:
        break;
    }
    return v;
}

char *af_blueprint_serialize_json(const af_blueprint *bp) {
    if (!bp)
        return NULL;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "format", "afterlight.blueprint");
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddStringToObject(root, "name", bp->name);
    cJSON_AddNumberToObject(root, "bound_object", (double)bp->bound_object);
    cJSON_AddBoolToObject(root, "enabled", bp->enabled);
    cJSON_AddNumberToObject(root, "next_node_id", bp->next_node_id);
    cJSON_AddNumberToObject(root, "next_link_id", bp->next_link_id);

    /* Variables */
    cJSON *vars = cJSON_CreateArray();
    for (size_t i = 0; i < bp->var_count; ++i) {
        cJSON *v = cJSON_CreateObject();
        cJSON_AddStringToObject(v, "name", bp->variables[i].name);
        cJSON_AddStringToObject(v, "type", pin_type_to_str(bp->variables[i].type));
        cJSON_AddItemToObject(v, "val", val_to_json(bp->variables[i].val));
        cJSON_AddItemToArray(vars, v);
    }
    cJSON_AddItemToObject(root, "variables", vars);

    /* Nodes */
    cJSON *nodes = cJSON_CreateArray();
    for (size_t i = 0; i < bp->node_count; ++i) {
        const af_bp_node *n = &bp->nodes[i];
        cJSON *node_j = cJSON_CreateObject();
        cJSON_AddNumberToObject(node_j, "id", n->id);
        cJSON_AddNumberToObject(node_j, "type", n->type);
        cJSON_AddStringToObject(node_j, "title", n->title);
        cJSON_AddNumberToObject(node_j, "x", (double)n->x);
        cJSON_AddNumberToObject(node_j, "y", (double)n->y);
        if (n->extra_str[0])
            cJSON_AddStringToObject(node_j, "extra", n->extra_str);

        cJSON *pins_j = cJSON_CreateArray();
        for (uint32_t p = 0; p < n->pin_count; ++p) {
            const af_bp_pin *pin = &n->pins[p];
            cJSON *pj = cJSON_CreateObject();
            cJSON_AddNumberToObject(pj, "id", pin->id);
            cJSON_AddStringToObject(pj, "name", pin->name);
            cJSON_AddStringToObject(pj, "type", pin_type_to_str(pin->type));
            cJSON_AddStringToObject(pj, "dir", pin->dir == AF_BP_PIN_INPUT ? "in" : "out");
            if (pin->dir == AF_BP_PIN_INPUT && pin->type != AF_BP_PIN_EXEC) {
                cJSON_AddItemToObject(pj, "def", val_to_json(pin->default_val));
            }
            cJSON_AddItemToArray(pins_j, pj);
        }
        cJSON_AddItemToObject(node_j, "pins", pins_j);
        cJSON_AddItemToArray(nodes, node_j);
    }
    cJSON_AddItemToObject(root, "nodes", nodes);

    /* Links */
    cJSON *links = cJSON_CreateArray();
    for (size_t i = 0; i < bp->link_count; ++i) {
        const af_bp_link *l = &bp->links[i];
        cJSON *lj = cJSON_CreateObject();
        cJSON_AddNumberToObject(lj, "id", l->id);
        cJSON_AddNumberToObject(lj, "from_node", l->from_node_id);
        cJSON_AddNumberToObject(lj, "from_pin", l->from_pin_id);
        cJSON_AddNumberToObject(lj, "to_node", l->to_node_id);
        cJSON_AddNumberToObject(lj, "to_pin", l->to_pin_id);
        cJSON_AddItemToArray(links, lj);
    }
    cJSON_AddItemToObject(root, "links", links);

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    return json_str;
}

af_blueprint *af_blueprint_deserialize_json(const char *json_text) {
    if (!json_text)
        return NULL;

    cJSON *root = cJSON_Parse(json_text);
    if (!root)
        return NULL;

    const cJSON *fmt = cJSON_GetObjectItemCaseSensitive(root, "format");
    if (!fmt || !cJSON_IsString(fmt) || strcmp(fmt->valuestring, "afterlight.blueprint") != 0) {
        cJSON_Delete(root);
        return NULL;
    }

    const cJSON *name_j = cJSON_GetObjectItemCaseSensitive(root, "name");
    af_blueprint *bp = af_blueprint_create(name_j ? name_j->valuestring : "Blueprint");
    if (!bp) {
        cJSON_Delete(root);
        return NULL;
    }

    const cJSON *bound_j = cJSON_GetObjectItemCaseSensitive(root, "bound_object");
    if (bound_j && cJSON_IsNumber(bound_j))
        bp->bound_object = (af_object_id)bound_j->valuedouble;

    const cJSON *en_j = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    if (en_j && cJSON_IsBool(en_j))
        bp->enabled = cJSON_IsTrue(en_j);

    const cJSON *nn_j = cJSON_GetObjectItemCaseSensitive(root, "next_node_id");
    if (nn_j && cJSON_IsNumber(nn_j))
        bp->next_node_id = (uint32_t)nn_j->valuedouble;

    const cJSON *nl_j = cJSON_GetObjectItemCaseSensitive(root, "next_link_id");
    if (nl_j && cJSON_IsNumber(nl_j))
        bp->next_link_id = (uint32_t)nl_j->valuedouble;

    /* Variables */
    const cJSON *vars_j = cJSON_GetObjectItemCaseSensitive(root, "variables");
    if (vars_j && cJSON_IsArray(vars_j)) {
        const cJSON *vj;
        cJSON_ArrayForEach(vj, vars_j) {
            const cJSON *vn = cJSON_GetObjectItemCaseSensitive(vj, "name");
            const cJSON *vt = cJSON_GetObjectItemCaseSensitive(vj, "type");
            const cJSON *vv = cJSON_GetObjectItemCaseSensitive(vj, "val");
            if (vn && vt) {
                af_bp_pin_type ptype = pin_type_from_str(vt->valuestring);
                af_bp_value pval = val_from_json(vv, ptype);
                af_blueprint_add_variable(bp, vn->valuestring, ptype, pval);
            }
        }
    }

    /* Nodes */
    const cJSON *nodes_j = cJSON_GetObjectItemCaseSensitive(root, "nodes");
    if (nodes_j && cJSON_IsArray(nodes_j)) {
        const cJSON *nj;
        cJSON_ArrayForEach(nj, nodes_j) {
            const cJSON *id_j = cJSON_GetObjectItemCaseSensitive(nj, "id");
            const cJSON *type_j = cJSON_GetObjectItemCaseSensitive(nj, "type");
            const cJSON *x_j = cJSON_GetObjectItemCaseSensitive(nj, "x");
            const cJSON *y_j = cJSON_GetObjectItemCaseSensitive(nj, "y");
            if (!id_j || !type_j)
                continue;

            af_bp_node_type ntype = (af_bp_node_type)(int)type_j->valuedouble;
            float nx = x_j ? (float)x_j->valuedouble : 0.0f;
            float ny = y_j ? (float)y_j->valuedouble : 0.0f;
            uint32_t nid = af_blueprint_add_node(bp, ntype, nx, ny);
            af_bp_node *node = af_blueprint_get_node(bp, nid);
            if (node) {
                node->id = (uint32_t)id_j->valuedouble;
                const cJSON *extra_j = cJSON_GetObjectItemCaseSensitive(nj, "extra");
                if (extra_j && cJSON_IsString(extra_j))
                    snprintf(node->extra_str, sizeof(node->extra_str), "%s", extra_j->valuestring);

                /* Pin default values */
                const cJSON *pins_j = cJSON_GetObjectItemCaseSensitive(nj, "pins");
                if (pins_j && cJSON_IsArray(pins_j)) {
                    const cJSON *pj;
                    cJSON_ArrayForEach(pj, pins_j) {
                        const cJSON *pid_j = cJSON_GetObjectItemCaseSensitive(pj, "id");
                        const cJSON *def_j = cJSON_GetObjectItemCaseSensitive(pj, "def");
                        if (pid_j && def_j) {
                            uint32_t pid = (uint32_t)pid_j->valuedouble;
                            for (uint32_t p = 0; p < node->pin_count; ++p) {
                                if (node->pins[p].id == pid) {
                                    node->pins[p].default_val = val_from_json(def_j, node->pins[p].type);
                                    node->pins[p].runtime_val = node->pins[p].default_val;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    /* Links */
    const cJSON *links_j = cJSON_GetObjectItemCaseSensitive(root, "links");
    if (links_j && cJSON_IsArray(links_j)) {
        const cJSON *lj;
        cJSON_ArrayForEach(lj, links_j) {
            const cJSON *fn_j = cJSON_GetObjectItemCaseSensitive(lj, "from_node");
            const cJSON *fp_j = cJSON_GetObjectItemCaseSensitive(lj, "from_pin");
            const cJSON *tn_j = cJSON_GetObjectItemCaseSensitive(lj, "to_node");
            const cJSON *tp_j = cJSON_GetObjectItemCaseSensitive(lj, "to_pin");
            if (fn_j && fp_j && tn_j && tp_j) {
                af_blueprint_add_link(bp, (uint32_t)fn_j->valuedouble, (uint32_t)fp_j->valuedouble,
                                      (uint32_t)tn_j->valuedouble, (uint32_t)tp_j->valuedouble);
            }
        }
    }

    cJSON_Delete(root);
    return bp;
}

bool af_blueprint_save_file(const af_blueprint *bp, const char *path) {
    if (!bp || !path)
        return false;
    char *text = af_blueprint_serialize_json(bp);
    if (!text)
        return false;
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(text);
        return false;
    }
    size_t len = strlen(text);
    bool ok = fwrite(text, 1, len, f) == len;
    fclose(f);
    free(text);
    return ok;
}

af_blueprint *af_blueprint_load_file(const char *path) {
    if (!path)
        return NULL;
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 16 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t read_bytes = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read_bytes] = '\0';

    af_blueprint *bp = af_blueprint_deserialize_json(buf);
    free(buf);
    return bp;
}
