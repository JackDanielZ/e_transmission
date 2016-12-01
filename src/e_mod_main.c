#define EFL_BETA_API_SUPPORT
#define EFL_EO_API_SUPPORT

#ifndef STAND_ALONE
#include <e.h>
#else
#include <Elementary.h>
#endif
#include <Ecore.h>
#include <Ecore_Con.h>
#include "e_mod_main.h"
#include "base64.h"

static const char *baseUrl = "http://%s:9091/transmission/rpc";
static void *_url_session_id_data_test = (void *)0;
static void *_url_torrents_stats_test = (void *)1;
static void *_url_torrents_add_test = (void *)2;

typedef struct
{
   const char *buffer;
   const char *current;
} Lexer;

typedef struct
{
#ifndef STAND_ALONE
   E_Gadcon_Client *gcc;
   E_Gadcon_Popup *popup;
   Config_Item *ci;
#endif
   Ecore_Timer *timer;
   Evas_Object *o_icon;
   Eo *main_box, *items_table, *no_conn_label, *error_label;
   Eina_List *items_list;
   char *session_id;
   char *torrents_data_buf;
   char *last_error;
   int torrents_data_buf_len;
   int torrents_data_len;

   Eina_Stringshare *ip_addr, *torrents_dir;
   Ecore_File_Monitor *torrents_dir_monitor;

   Eina_Bool reload : 1;
} Instance;

typedef struct
{
   Instance *inst;
   const char *name;
   unsigned int size, downrate, uprate, id, status;
   double done, ratio;

   int table_idx;
   Eo *name_label;
   Eo *size_label;
   Eo *done_label;
   Eo *downrate_label, *uprate_label, *ratio_label;
   Eo *start_button, *start_icon, *pause_icon;
   Eo *del_button, *del_icon;
   Eo *delall_button, *delall_icon;

   Eina_Bool alive : 1;
} Item_Desc;

#ifndef STAND_ALONE
static E_Config_DD *conf_edd = NULL;
static E_Config_DD *conf_item_edd = NULL;
#endif

static Eina_List *instances = NULL;

#ifndef STAND_ALONE
Config *cpu_conf = NULL;

static void
_menu_cb_post(void *data EINA_UNUSED, E_Menu *m EINA_UNUSED)
{
   if (!cpu_conf->menu) return;
   e_object_del(E_OBJECT(cpu_conf->menu));
   cpu_conf->menu = NULL;
   if (cpu_conf->menu_interval)
     e_object_del(E_OBJECT(cpu_conf->menu_interval));
   cpu_conf->menu_interval = NULL;
}
#endif

static void
_size_to_string(double size, const char *suffix, char *out)
{
   const char* units[] = {"B", "KB", "MB", "GB", NULL};
   const char* formats[] = {"%5.0f%s%s", "%5.1f%s%s", "%5.1f%s%s", "%5.1f%s%s", NULL};
   unsigned int idx = 0;
   while (size >= 1000)
     {
        size /= 1000;
        idx++;
     }
   sprintf(out, formats[idx], size, units[idx], suffix ? suffix : "");
}

enum
{
   NAME_COL,
   SIZE_COL,
   DONE_COL,
   DOWNRATE_COL,
   UPRATE_COL,
   RATIO_COL,
   PLAY_COL,
   DEL_COL,
   DELALL_COL,
};

static Eo *
_label_create(Eo *parent, const char *text, Eo **wref)
{
   Eo *label = wref ? *wref : NULL;
   if (!label)
     {
        label = elm_label_add(parent);
        evas_object_size_hint_align_set(label, 0.0, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(label, EVAS_HINT_EXPAND, 0.0);
        evas_object_show(label);
        if (wref) efl_wref_add(label, wref);
     }
   elm_object_text_set(label, text);
   return label;
}

static Eo *
_button_create(Eo *parent, const char *text, Eo *icon, Eo **wref, Evas_Smart_Cb cb_func, void *cb_data)
{
   Eo *bt = wref ? *wref : NULL;
   if (!bt)
     {
        bt = elm_button_add(parent);
        evas_object_size_hint_align_set(bt, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(bt, EVAS_HINT_EXPAND, 0.0);
        evas_object_show(bt);
        if (wref) efl_wref_add(bt, wref);
        if (cb_func) evas_object_smart_callback_add(bt, "clicked", cb_func, cb_data);
     }
   elm_object_text_set(bt, text);
   elm_object_part_content_set(bt, "icon", icon);
   return bt;
}

static Eo *
_icon_create(Eo *parent, const char *path, Eo **wref)
{
   Eo *ic = wref ? *wref : NULL;
   if (!ic)
     {
        ic = elm_icon_add(parent);
        elm_icon_standard_set(ic, path);
        evas_object_show(ic);
        if (wref) efl_wref_add(ic, wref);
     }
   return ic;
}

static Ecore_Con_Url *
_url_create(const char *url)
{
   Ecore_Con_Url *ec_url = ecore_con_url_new(url);
   if (!ec_url) return NULL;
   ecore_con_url_proxy_set(ec_url, NULL);
   ecore_con_url_additional_header_add(ec_url, "Accept-Encoding", "identity");
   return ec_url;
}

static void
_start_pause_bt_clicked(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Item_Desc *d = data;
   Instance *inst = d->inst;
   char request[256];
   char url[1024];
   if (!inst->session_id) return;
   sprintf(request, "{\"method\":\"torrent-%s\", \"arguments\":{\"ids\":[%d]}}",
         d->status ? "stop" : "start", d->id);
   sprintf(url, baseUrl, inst->ip_addr);
   Ecore_Con_Url *ec_url = _url_create(url);
   if (!ec_url) return;
   ecore_con_url_additional_header_add(ec_url, "X-Transmission-Session-Id", inst->session_id);
   ecore_con_url_post(ec_url, request, strlen(request), NULL);
}

static void
_del_bt_clicked(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Item_Desc *d = data;
   Instance *inst = d->inst;
   char request[256];
   char url[1024];
   if (!inst->session_id) return;
   sprintf(request,
         "{\"method\":\"torrent-remove\", "
         "\"arguments\":{\"ids\":[%d],\"delete-local-data\":false}}",
         d->id);
   sprintf(url, baseUrl, inst->ip_addr);
   Ecore_Con_Url *ec_url = _url_create(url);
   if (!ec_url) return;
   ecore_con_url_additional_header_add(ec_url, "X-Transmission-Session-Id", inst->session_id);
   ecore_con_url_post(ec_url, request, strlen(request), NULL);
}

static void
_delall_bt_clicked(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Item_Desc *d = data;
   Instance *inst = d->inst;
   char request[256];
   char url[1024];
   if (!inst->session_id) return;
   sprintf(request,
         "{\"method\":\"torrent-remove\", "
         "\"arguments\":{\"ids\":[%d],\"delete-local-data\":true}}",
         d->id);
   sprintf(url, baseUrl, inst->ip_addr);
   Ecore_Con_Url *ec_url = _url_create(url);
   if (!ec_url) return;
   ecore_con_url_additional_header_add(ec_url, "X-Transmission-Session-Id", inst->session_id);
   ecore_con_url_post(ec_url, request, strlen(request), NULL);
}

static void
_box_update(Instance *inst)
{
   char str[128];;
   Eina_List *itr, *itr2;
   Item_Desc *d, *d2;

   if (!inst->main_box) return;

   if (!inst->items_list)
     {
        elm_box_clear(inst->main_box);
        _label_create(inst->main_box, "No connection", &inst->no_conn_label);
        elm_box_pack_end(inst->main_box, inst->no_conn_label);
        return;
     }
   else
     {
        efl_unref(inst->no_conn_label);
     }

   if (inst->last_error)
     {
        elm_box_clear(inst->main_box);
        _label_create(inst->main_box, inst->last_error, &inst->error_label);
        elm_box_pack_end(inst->main_box, inst->error_label);
     }

   if (!inst->items_table)
     {
        Eo *o = elm_table_add(inst->main_box);
        evas_object_size_hint_align_set(o, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(o, EVAS_HINT_EXPAND, 0.0);
        elm_table_padding_set(o, 20, 0);
        elm_box_pack_end(inst->main_box, o);
        evas_object_show(o);
        efl_wref_add(o, &inst->items_table);
        inst->reload = EINA_TRUE;
     }

   if (inst->reload)
     {
        elm_table_clear(inst->items_table, EINA_TRUE);

        elm_table_pack(inst->items_table,
              _label_create(inst->items_table, "<b>Torrent name</b>", NULL),
              NAME_COL, 0, 1, 1);
        elm_table_pack(inst->items_table,
              _label_create(inst->items_table, "<b>Size</b>", NULL),
              SIZE_COL, 0, 1, 1);
        elm_table_pack(inst->items_table,
              _label_create(inst->items_table, "<b>Done</b>", NULL),
              DONE_COL, 0, 1, 1);
        elm_table_pack(inst->items_table,
              _label_create(inst->items_table, "<b>Download</b>", NULL),
              DOWNRATE_COL, 0, 1, 1);
        elm_table_pack(inst->items_table,
              _label_create(inst->items_table, "<b>Upload</b>", NULL),
              UPRATE_COL, 0, 1, 1);
        elm_table_pack(inst->items_table,
              _label_create(inst->items_table, "<b>Ratio</b>", NULL),
              RATIO_COL, 0, 1, 1);

        inst->reload = EINA_FALSE;
     }

   EINA_LIST_FOREACH(inst->items_list, itr, d)
     {
        if (!d->table_idx)
          {
             Eina_Bool found = EINA_TRUE;
             while (found)
               {
                  found = EINA_FALSE;
                  d->table_idx++;
                  EINA_LIST_FOREACH(inst->items_list, itr2, d2)
                     if (d != d2 && d2->table_idx == d->table_idx) found = EINA_TRUE;
               }
          }
        _label_create(inst->items_table, d->name, &d->name_label);
        elm_table_pack(inst->items_table, d->name_label, NAME_COL, d->table_idx, 1, 1);

        _size_to_string(d->size, NULL, str);
        _label_create(inst->items_table, str, &d->size_label);
        elm_table_pack(inst->items_table, d->size_label, SIZE_COL, d->table_idx, 1, 1);

        sprintf(str, "%5.1f%%", d->done);
        _label_create(inst->items_table, str, &d->done_label);
        elm_table_pack(inst->items_table, d->done_label, DONE_COL, d->table_idx, 1, 1);

        _size_to_string(d->downrate, "/s", str);
        _label_create(inst->items_table, d->downrate ? str : "---", &d->downrate_label);
        elm_table_pack(inst->items_table, d->downrate_label, DOWNRATE_COL, d->table_idx, 1, 1);

        _size_to_string(d->uprate, "/s", str);
        _label_create(inst->items_table, d->uprate ? str : "---", &d->uprate_label);
        elm_table_pack(inst->items_table, d->uprate_label, UPRATE_COL, d->table_idx, 1, 1);

        sprintf(str, "%5.1f", d->ratio);
        _label_create(inst->items_table, str, &d->ratio_label);
        elm_table_pack(inst->items_table, d->ratio_label, RATIO_COL, d->table_idx, 1, 1);

        _icon_create(inst->items_table, "media-playback-start", &d->start_icon);
        _icon_create(inst->items_table, "media-playback-pause", &d->pause_icon);
        _button_create(inst->items_table, NULL, d->status ? d->pause_icon : d->start_icon, &d->start_button, _start_pause_bt_clicked, d);
        elm_table_pack(inst->items_table, d->start_button, PLAY_COL, d->table_idx, 1, 1);

        _icon_create(inst->items_table, "edit-delete", &d->del_icon);
        _button_create(inst->items_table, NULL, d->del_icon, &d->del_button, _del_bt_clicked, d);
        elm_table_pack(inst->items_table, d->del_button, DEL_COL, d->table_idx, 1, 1);

        _icon_create(inst->items_table, "application-exit", &d->delall_icon);
        _button_create(inst->items_table, NULL, d->delall_icon, &d->delall_button, _delall_bt_clicked, d);
        elm_table_pack(inst->items_table, d->delall_button, DELALL_COL, d->table_idx, 1, 1);
     }
}

#ifndef STAND_ALONE
static void
_popup_del(Instance *inst)
{
   E_FREE_FUNC(inst->popup, e_object_del);
}

static void
_popup_del_cb(void *obj)
{
   _popup_del(e_object_data_get(obj));
}

static void
_popup_comp_del_cb(void *data, Evas_Object *obj EINA_UNUSED)
{
   Instance *inst = data;

   E_FREE_FUNC(inst->popup, e_object_del);
}

static void
_button_cb_mouse_down(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Instance *inst;
   Evas_Event_Mouse_Down *ev;

   inst = data;
   ev = event_info;
   if (ev->button == 1)
     {
        if (!inst->popup)
          {
             Evas_Object *o;
             inst->popup = e_gadcon_popup_new(inst->gcc, 0);

             o = elm_box_add(e_comp->elm);
             evas_object_size_hint_align_set(o, EVAS_HINT_FILL, EVAS_HINT_FILL);
             evas_object_size_hint_weight_set(o, EVAS_HINT_EXPAND, 0.0);
             evas_object_show(o);
             efl_wref_add(o, &inst->main_box);

             _box_update(inst);

             e_gadcon_popup_content_set(inst->popup, inst->main_box);
             e_comp_object_util_autoclose(inst->popup->comp_object,
                   _popup_comp_del_cb, NULL, inst);
             e_gadcon_popup_show(inst->popup);
             e_object_data_set(E_OBJECT(inst->popup), inst);
             E_OBJECT_DEL_SET(inst->popup, _popup_del_cb);
          }
     }
   else if ((ev->button == 3) && (!cpu_conf->menu))
     {
	E_Menu *m, *mo;
	E_Menu_Item *mi;
	int cx, cy, cw, ch;

        m = e_menu_new();

	mo = e_menu_new();
	cpu_conf->menu_interval = mo;

	mi = e_menu_item_new(mo);
	e_menu_item_label_set(mi, "Fast (0.5 sec)");
	e_menu_item_radio_set(mi, 1);
	e_menu_item_radio_group_set(mi, 1);
	//if (inst->ci->interval <= 0.5) e_menu_item_toggle_set(mi, 1);
	//e_menu_item_callback_set(mi, _cpu_menu_fast, inst);

	mi = e_menu_item_new(mo);
	e_menu_item_label_set(mi, "Medium (1 sec)");
	e_menu_item_radio_set(mi, 1);
	e_menu_item_radio_group_set(mi, 1);

	mi = e_menu_item_new(mo);
	e_menu_item_label_set(mi, "Normal (2 sec)");
	e_menu_item_radio_set(mi, 1);
	e_menu_item_radio_group_set(mi, 1);

	mi = e_menu_item_new(mo);
	e_menu_item_label_set(mi, "Slow (5 sec)");
	e_menu_item_radio_set(mi, 1);
	e_menu_item_radio_group_set(mi, 1);

	mi = e_menu_item_new(mo);
	e_menu_item_label_set(mi, "Very Slow (30 sec)");
	e_menu_item_radio_set(mi, 1);
	e_menu_item_radio_group_set(mi, 1);

        mi = e_menu_item_new(m);
	e_menu_item_label_set(mi, "Time Between Updates");

        m = e_gadcon_client_util_menu_items_append(inst->gcc, m, 0);
	e_menu_post_deactivate_callback_set(m, _menu_cb_post, inst);
	cpu_conf->menu = m;

	e_gadcon_canvas_zone_geometry_get(inst->gcc->gadcon, &cx, &cy, &cw, &ch);
	e_menu_activate_mouse(m,
			      e_zone_current_get(),
			      cx + ev->output.x, cy + ev->output.y, 1, 1,
			      E_MENU_POP_DIRECTION_DOWN, ev->timestamp);
	evas_event_feed_mouse_up(inst->gcc->gadcon->evas, ev->button,
				 EVAS_BUTTON_NONE, ev->timestamp, NULL);
     }
}
#endif

static Eina_Bool
_torrent_add(Instance *inst, const char *file)
{
   char url[1024], full_path[256];
   char *content = NULL, *ret_content = NULL, *request = NULL;
   int filesize, retsize;
   Eina_Bool ret = EINA_FALSE;
   if (!inst->session_id) goto end;

   sprintf(full_path, "%s/%s", inst->torrents_dir, file);
   FILE *fp = fopen(full_path, "rb");
   if (!fp) goto end;
   fseek(fp, 0, SEEK_END);
   filesize = ftell(fp);
   if (filesize < 0) goto end;
   fseek(fp, 0, SEEK_SET);
   content = malloc(filesize + 1);
   if (fread(content, filesize, 1, fp) != 1) goto end;
   content[filesize] = '\0';
   fclose(fp);

   ret_content = base64_encode(content, filesize, &retsize);
   request = malloc(retsize + 256);
   sprintf(request,
         "{\"method\":\"torrent-add\", "
         "\"arguments\":{\"metainfo\":\"%s\"}}", ret_content);
   sprintf(url, baseUrl, inst->ip_addr);
   Ecore_Con_Url *ec_url = _url_create(url);
   if (!ec_url) goto end;
   ecore_con_url_additional_header_add(ec_url, "X-Transmission-Session-Id", inst->session_id);
   ecore_con_url_data_set(ec_url, &_url_torrents_add_test);
   efl_key_data_set(ec_url, "Transmission_Instance", inst);
   efl_key_data_set(ec_url, "Transmission_FileToRemove", eina_stringshare_add(full_path));
   ecore_con_url_post(ec_url, request, strlen(request), NULL);
   ret = EINA_TRUE;
end:
   free(content);
   free(ret_content);
   free(request);
   return ret;
}

static void
_torrents_dir_changed(void *data,
      Ecore_File_Monitor *em EINA_UNUSED,
      Ecore_File_Event event EINA_UNUSED, const char *path EINA_UNUSED)
{
   Instance *inst = data;
   Eina_List *l = ecore_file_ls(inst->torrents_dir);
   char *file;
   Eina_Bool stop = EINA_FALSE;
   EINA_LIST_FREE(l, file)
     {
        if (!stop && eina_str_has_suffix(file, ".torrent"))
          {
             stop = _torrent_add(inst, file);
          }
        free(file);
     }
}

static void
_torrent_added_cb(void *data EINA_UNUSED, const Efl_Event *ev)
{
   Instance *inst = efl_key_data_get(ev->object, "Transmission_Instance");
   Eina_Stringshare *name = efl_key_data_get(ev->object, "Transmission_FileToRemove");
   remove(name);
   eina_stringshare_del(name);
   _torrents_dir_changed(inst, NULL, ECORE_FILE_EVENT_MODIFIED, NULL);
}

static Instance *
_instance_create(void)
{
   Instance *inst = calloc(1, sizeof(Instance));
   inst->ip_addr = "127.0.0.1";
   inst->torrents_dir = "/home/daniel/Downloads";
   inst->torrents_dir_monitor = ecore_file_monitor_add(inst->torrents_dir,
         _torrents_dir_changed, inst);
   instances = eina_list_append(instances, inst);
   return inst;
}

#ifndef STAND_ALONE
static E_Gadcon_Client *
_gc_init(E_Gadcon *gc, const char *name, const char *id, const char *style)
{
   Instance *inst;
   E_Gadcon_Client *gcc;
   char buf[4096];

   inst = _instance_create();
   snprintf(buf, sizeof(buf), "%s/transmission.edj", e_module_dir_get(cpu_conf->module));

   inst->o_icon = edje_object_add(gc->evas);
   if (!e_theme_edje_object_set(inst->o_icon,
				"base/theme/modules/transmission",
                                "modules/transmission/main"))
      edje_object_file_set(inst->o_icon, buf, "modules/transmission/main");
   evas_object_show(inst->o_icon);

   gcc = e_gadcon_client_new(gc, name, id, style, inst->o_icon);
   gcc->data = inst;
   inst->gcc = gcc;

   evas_object_event_callback_add(inst->o_icon, EVAS_CALLBACK_MOUSE_DOWN,
				  _button_cb_mouse_down, inst);

   return gcc;
}
#endif

static void
_instance_delete(Instance *inst)
{
   if (inst->timer) ecore_timer_del(inst->timer);
   if (inst->o_icon) evas_object_del(inst->o_icon);
   if (inst->main_box) evas_object_del(inst->main_box);

   instances = eina_list_remove(instances, inst);
   free(inst);
}

#ifndef STAND_ALONE
static void
_gc_shutdown(E_Gadcon_Client *gcc)
{
   _instance_delete(gcc->data);
}

static void
_gc_orient(E_Gadcon_Client *gcc, E_Gadcon_Orient orient EINA_UNUSED)
{
   e_gadcon_client_aspect_set(gcc, 32, 16);
   e_gadcon_client_min_size_set(gcc, 32, 16);
}

static const char *
_gc_label(const E_Gadcon_Client_Class *client_class EINA_UNUSED)
{
   return "Transmission";
}

static Evas_Object *
_gc_icon(const E_Gadcon_Client_Class *client_class EINA_UNUSED, Evas *evas)
{
   Evas_Object *o;
   char buf[4096];

   if (!cpu_conf->module) return NULL;

   snprintf(buf, sizeof(buf), "%s/e-module-transmission.edj", e_module_dir_get(cpu_conf->module));

   o = edje_object_add(evas);
   edje_object_file_set(o, buf, "icon");
   return o;
}

static const char *
_gc_id_new(const E_Gadcon_Client_Class *client_class)
{
   char buf[32];
   static int id = 0;
   sprintf(buf, "%s.%d", client_class->name, ++id);
   return eina_stringshare_add(buf);
}
#endif

static void
_lexer_reset(Lexer *l)
{
   l->current = l->buffer;
}

static void
_ws_skip(Lexer *l)
{
   /*
    * Skip spaces and \n
    */
   do
     {
        char c = *(l->current);
        switch (c)
          {
           case ' ': case '\n':
              break;
           default:
              return;
          }
        l->current++;
     }
   while (1);
}

static Eina_Bool
_is_next_token(Lexer *l, const char *token)
{
   _ws_skip(l);
   if (!strncmp(l->current, token, strlen(token)))
     {
        l->current += strlen(token);
        return EINA_TRUE;
     }
   return EINA_FALSE;
}

static char *
_next_word(Lexer *l, const char *special, Eina_Bool special_allowed)
{
   if (!special) special = "";
   _ws_skip(l);
   const char *str = l->current;
   while (*str &&
         ((*str >= 'a' && *str <= 'z') ||
          (*str >= 'A' && *str <= 'Z') ||
          (*str >= '0' && *str <= '9') ||
          !(!!special_allowed ^ !!strchr(special, *str)) ||
          *str == '_')) str++;
   if (str == l->current) return NULL;
   int size = str - l->current;
   char *word = malloc(size + 1);
   memcpy(word, l->current, size);
   word[size] = '\0';
   l->current = str;
   return word;
}

static int
_next_integer(Lexer *l)
{
   _ws_skip(l);
   const char *str = l->current;
   while (*str && (*str >= '0' && *str <= '9')) str++;
   if (str == l->current) return -1;
   int size = str - l->current;
   char *n_str = alloca(size + 1);
   memcpy(n_str, l->current, size);
   n_str[size] = '\0';
   l->current = str;
   return atoi(n_str);
}

static double
_next_double(Lexer *l)
{
   _ws_skip(l);
   const char *str = l->current;
   while (*str && ((*str >= '0' && *str <= '9') || *str == '.')) str++;
   if (str == l->current) return -1;
   int size = str - l->current;
   char *n_str = alloca(size + 1);
   memcpy(n_str, l->current, size);
   n_str[size] = '\0';
   l->current = str;
   return atof(n_str);
}

#define JUMP_AT(l, ...) _jump_at(l, __VA_ARGS__, NULL)

static Eina_Bool
_jump_at(Lexer *l, ...)
{
   const char *token;
   Eina_Bool over;
   char *min = NULL;
   va_list args;
   va_start(args, l);
   do
     {
        token = va_arg(args, const char *);
        over = va_arg(args, int);
        if (token)
          {
             char *found = strstr(l->current, token);
             if (found) found += (over ? strlen(token) : 0);
             if (found) min = (!min || found < min ? found : min);
          }
     } while (token);
   if (!min) return EINA_FALSE;
   l->current = min;
   return EINA_TRUE;
}

#ifndef STAND_ALONE
EAPI E_Module_Api e_modapi =
{
   E_MODULE_API_VERSION, "Transmission"
};

static const E_Gadcon_Client_Class _gc_class =
{
   GADCON_CLIENT_CLASS_VERSION, "transmission",
   {
      _gc_init, _gc_shutdown, _gc_orient, _gc_label, _gc_icon, _gc_id_new, NULL, NULL
   },
   E_GADCON_CLIENT_STYLE_PLAIN
};
#endif

static Eina_Bool
_session_id_get_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event_info)
{
   Ecore_Con_Event_Url_Complete *url_complete = event_info;
   Ecore_Con_Url *ec_url = url_complete->url_con;
   Instance *inst = efl_key_data_get(ec_url, "Transmission_Instance");
   void **test = ecore_con_url_data_get(ec_url);
   if (!inst || !test || *test != _url_session_id_data_test) return EINA_TRUE;

   if (url_complete->status)
     {
        if (!inst->session_id)
          {
             const Eina_List *hdrs = ecore_con_url_response_headers_get(ec_url), *itr;
             char *hdr;
             EINA_LIST_FOREACH(hdrs, itr, hdr)
               {
                  if (strstr(hdr, "X-Transmission-Session-Id"))
                    {
                       char *tmp;
                       inst->session_id = strdup(hdr + 27);
                       tmp = inst->session_id;
                       while (*tmp)
                         {
                            if (*tmp == 0x0D) *tmp = '\0';
                            else tmp++;
                         }
                    }
               }
             _torrents_dir_changed(inst, NULL, ECORE_FILE_EVENT_MODIFIED, NULL);
          }
     }
   else
     {
        free(inst->session_id);
        inst->session_id = NULL;
     }

   return EINA_FALSE;
}

static Eina_Bool
_session_id_poller_cb(void *data EINA_UNUSED)
{
   Eina_List *itr;
   Instance *inst;
   EINA_LIST_FOREACH(instances, itr, inst)
     {
        char url[1024];
        sprintf(url, baseUrl, inst->ip_addr);
        Ecore_Con_Url *ec_url = _url_create(url);
        if (!ec_url) return EINA_TRUE;
        ecore_con_url_data_set(ec_url, &_url_session_id_data_test);
        efl_key_data_set(ec_url, "Transmission_Instance", inst);
        ecore_con_url_get(ec_url);
     }
   return EINA_TRUE;
}

static Item_Desc *
_item_find_by_name(const Eina_List *lst, const char *name)
{
   const Eina_List *itr;
   Item_Desc *d;
   EINA_LIST_FOREACH(lst, itr, d)
     {
        if (!strcmp(d->name, name)) return d;
     }
   return NULL;
}

static Eina_Bool
_json_data_parse(Instance *inst)
{
   Lexer l;
   l.buffer = inst->torrents_data_buf;
   _lexer_reset(&l);
   if (!_is_next_token(&l, "{")) return EINA_FALSE;
   if (_is_next_token(&l, "\"arguments\":{"))
     {
        if (_is_next_token(&l, "\"torrents\":["))
          {
             while (!_is_next_token(&l, "]"))
               {
                  char *name = NULL;
                  int leftuntildone = 0, id = 0, status = 0, uprate = 0, downrate = 0;
                  unsigned int size = 0;
                  double ratio = 0.0;
                  if (!_is_next_token(&l, "{")) return EINA_FALSE;
                  while (!_is_next_token(&l, "}"))
                    {
                       if (_is_next_token(&l, "\"name\":\""))
                         {
                            name = _next_word(&l, ".[]_- ", EINA_TRUE);
                            JUMP_AT(&l, ",", EINA_TRUE, "}", EINA_FALSE);
                         }
                       else if (_is_next_token(&l, "\"id\":"))
                         {
                            id = _next_integer(&l);
                            JUMP_AT(&l, ",", EINA_TRUE, "}", EINA_FALSE);
                         }
                       else if (_is_next_token(&l, "\"status\":"))
                         {
                            status = _next_integer(&l);
                            JUMP_AT(&l, ",", EINA_TRUE, "}", EINA_FALSE);
                         }
                       else if (_is_next_token(&l, "\"leftUntilDone\":"))
                         {
                            leftuntildone = _next_integer(&l);
                            JUMP_AT(&l, ",", EINA_TRUE, "}", EINA_FALSE);
                         }
                       else if (_is_next_token(&l, "\"rateDownload\":"))
                         {
                            downrate = _next_integer(&l);
                            JUMP_AT(&l, ",", EINA_TRUE, "}", EINA_FALSE);
                         }
                       else if (_is_next_token(&l, "\"rateUpload\":"))
                         {
                            uprate = _next_integer(&l);
                            JUMP_AT(&l, ",", EINA_TRUE, "}", EINA_FALSE);
                         }
                       else if (_is_next_token(&l, "\"sizeWhenDone\":"))
                         {
                            size = _next_integer(&l);
                            JUMP_AT(&l, ",", EINA_TRUE, "}", EINA_FALSE);
                         }
                       else if (_is_next_token(&l, "\"uploadRatio\":"))
                         {
                            ratio = _next_double(&l);
                            JUMP_AT(&l, ",", EINA_TRUE, "}", EINA_FALSE);
                         }
                       else
                         {
                            if (!JUMP_AT(&l, "}", EINA_FALSE)) return EINA_FALSE;
                         }
                    }
                  if (id)
                    {
                       Item_Desc *d = _item_find_by_name(inst->items_list, name);
                       if (!d)
                         {
                            d = calloc(1, sizeof(Item_Desc));
                            d->inst = inst;
                            d->id = id;
                            d->name = eina_stringshare_add(name);
                            inst->items_list = eina_list_append(inst->items_list, d);
                            inst->reload = EINA_TRUE;
                         }
                       d->alive = EINA_TRUE;
                       d->size = size;
                       d->downrate = downrate;
                       d->uprate = uprate;
                       d->ratio = ratio;
                       d->status = status;
                       d->done = 100.0 - (100 * ((double)leftuntildone / size));
                       free(name);
                    }
                  _is_next_token(&l, ",");
               }
          }
     }
   return EINA_TRUE;
}

static void
_items_clear(Instance *inst)
{
   Eina_List *itr, *itr2;
   Item_Desc *d;
   EINA_LIST_FOREACH_SAFE(inst->items_list, itr, itr2, d)
     {
        if (!d->alive)
          {
             evas_object_del(inst->items_table);
             inst->items_list = eina_list_remove_list(inst->items_list, itr);
             inst->reload = EINA_TRUE;
             free(d);
          }
        else d->alive = EINA_FALSE;
     }
}

static Eina_Bool
_torrents_data_get_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event_info)
{
   Ecore_Con_Event_Url_Data *url_data = event_info;
   Ecore_Con_Url *ec_url = url_data->url_con;
   Instance *inst = efl_key_data_get(ec_url, "Transmission_Instance");
   void **test = ecore_con_url_data_get(ec_url);
   if (!inst || !test ||
         (*test != _url_torrents_stats_test && *test != _url_torrents_add_test))
         return EINA_TRUE;
   if (url_data->size > (inst->torrents_data_buf_len - inst->torrents_data_len))
     {
        inst->torrents_data_buf_len = inst->torrents_data_len + url_data->size;
        inst->torrents_data_buf = realloc(inst->torrents_data_buf,
              inst->torrents_data_buf_len + 1);
     }
   memcpy(inst->torrents_data_buf + inst->torrents_data_len,
         url_data->data, url_data->size);
   inst->torrents_data_len += url_data->size;
   inst->torrents_data_buf[inst->torrents_data_len] = '\0';
   return EINA_FALSE;
}

static Eina_Bool
_torrents_data_status_get_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event_info)
{
   Ecore_Con_Event_Url_Complete *url_complete = event_info;
   Ecore_Con_Url *ec_url = url_complete->url_con;
   Instance *inst = efl_key_data_get(ec_url, "Transmission_Instance");
   void **test = ecore_con_url_data_get(ec_url);
   if (!inst || !test ||
         (*test != _url_torrents_stats_test && *test != _url_torrents_add_test))
         return EINA_TRUE;
   char *result_str = strstr(inst->torrents_data_buf, "\"result\":");
   if (!result_str) return EINA_FALSE;
   if (strncmp(result_str + strlen("\"result\":"), "\"success\"", 9))
     {
        if (inst->last_error) free(inst->last_error);
        char *end = strchr(result_str + strlen("\"result\":\""), '\"');
        inst->last_error = malloc(end - result_str + 1);
        strncpy(inst->last_error, result_str, end - result_str);
        inst->torrents_data_len = 0;
        return EINA_FALSE;
     }
   if (*test == _url_torrents_stats_test)
     {
        _json_data_parse(inst);
        _items_clear(inst);
        _box_update(inst);
        inst->torrents_data_len = 0;
     }
   if (*test == _url_torrents_add_test)
     {
        Eina_Stringshare *name = efl_key_data_get(ec_url, "Transmission_FileToRemove");
        remove(name);
        eina_stringshare_del(name);
        _torrents_dir_changed(inst, NULL, ECORE_FILE_EVENT_MODIFIED, NULL);
     }
   return EINA_FALSE;
}

static Eina_Bool
_torrents_poller_cb(void *data EINA_UNUSED)
{
   Eina_List *itr;
   Instance *inst;
   const char *fields_list = "{\"arguments\":{\"fields\":[\"name\", \"status\", \"id\", \"leftUntilDone\", \"rateDownload\", \"rateUpload\", \"sizeWhenDone\", \"uploadRatio\"]}, \"method\":\"torrent-get\"}";
   int len = strlen(fields_list);
   EINA_LIST_FOREACH(instances, itr, inst)
     {
        char url[1024];
        if (!inst->session_id)
          {
             _items_clear(inst);
             _box_update(inst);
             continue;
          }
        sprintf(url, baseUrl, inst->ip_addr);
        Ecore_Con_Url *ec_url = _url_create(url);
        if (!ec_url) continue;
        ecore_con_url_data_set(ec_url, &_url_torrents_stats_test);
        efl_key_data_set(ec_url, "Transmission_Instance", inst);

        ecore_con_url_additional_header_add(ec_url, "X-Transmission-Session-Id", inst->session_id);
        //ecore_con_url_additional_header_add(ec_url, "Content-Length", len);

        ecore_con_url_post(ec_url, fields_list, len, NULL);
     }
   return EINA_TRUE;
}

#ifndef STAND_ALONE
EAPI void *
e_modapi_init(E_Module *m)
{
   ecore_init();
   ecore_con_init();
   ecore_con_url_init();

   conf_item_edd = E_CONFIG_DD_NEW("Cpu_Config_Item", Config_Item);
   conf_edd = E_CONFIG_DD_NEW("Cpu_Config", Config);

   #undef T
   #define T Config_Item
   #undef D
   #define D conf_item_edd
   E_CONFIG_VAL(D, T, id, STR);
   E_CONFIG_VAL(D, T, interval, DOUBLE);
   E_CONFIG_VAL(D, T, merge_cpus, INT);

   #undef T
   #define T Config
   #undef D
   #define D conf_edd
   E_CONFIG_LIST(D, T, items, conf_item_edd);

   cpu_conf = e_config_domain_load("module.transmission", conf_edd);
   if (!cpu_conf)
     {
	Config_Item *ci;

	cpu_conf = E_NEW(Config, 1);
	ci = E_NEW(Config_Item, 1);
	ci->id = eina_stringshare_add("0");
	ci->interval = 1;
	ci->merge_cpus = 0;

	cpu_conf->items = eina_list_append(cpu_conf->items, ci);
     }

   cpu_conf->module = m;
   e_gadcon_provider_register(&_gc_class);

   ecore_event_handler_add(ECORE_CON_EVENT_URL_COMPLETE, _session_id_get_cb, NULL);
   ecore_event_handler_add(ECORE_CON_EVENT_URL_DATA, _torrents_data_get_cb, NULL);
   ecore_event_handler_add(ECORE_CON_EVENT_URL_COMPLETE, _torrents_data_status_get_cb, NULL);
   ecore_timer_add(1.0, _session_id_poller_cb, NULL);
   ecore_timer_add(1.0, _torrents_poller_cb, NULL);
   _session_id_poller_cb(NULL);
   return m;
}

EAPI int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
   cpu_conf->module = NULL;
   e_gadcon_provider_unregister(&_gc_class);
   if (cpu_conf->config_dialog)
     e_object_del(E_OBJECT(cpu_conf->config_dialog));
   if (cpu_conf->menu)
     {
	e_menu_post_deactivate_callback_set(cpu_conf->menu, NULL, NULL);
	e_object_del(E_OBJECT(cpu_conf->menu));
	cpu_conf->menu = NULL;
     }

   while (cpu_conf->items)
     {
	Config_Item *ci = cpu_conf->items->data;
	if (ci->id) eina_stringshare_del(ci->id);
	cpu_conf->items = eina_list_remove_list(cpu_conf->items, cpu_conf->items);
	E_FREE(ci);
     }

   E_FREE(cpu_conf);
   E_CONFIG_DD_FREE(conf_item_edd);
   E_CONFIG_DD_FREE(conf_edd);

   ecore_con_url_shutdown();
   ecore_con_shutdown();
   ecore_shutdown();
   return 1;
}

EAPI int
e_modapi_save(E_Module *m EINA_UNUSED)
{
   e_config_domain_save("module.transmission", conf_edd, cpu_conf);
   return 1;
}
#endif

#ifdef STAND_ALONE
int main(int argc, char **argv)
{
   eina_init();
   ecore_init();
   ecore_con_init();
   elm_init(argc, argv);
   Instance *inst = _instance_create();

   ecore_timer_add(1.0, _session_id_poller_cb, NULL);
   ecore_timer_add(1.0, _torrents_poller_cb, NULL);
   _session_id_poller_cb(NULL);

   Eo *win = elm_win_add(NULL, "Transmission", ELM_WIN_BASIC);

   Eo *o = elm_box_add(win);
   evas_object_size_hint_align_set(o, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(o, EVAS_HINT_EXPAND, 0.0);
   evas_object_show(o);
   elm_win_resize_object_add(win, o);
   efl_wref_add(o, &inst->main_box);

   evas_object_resize(win, 480, 480);
   evas_object_show(win);
   elm_run();

   _instance_delete(inst);
   elm_shutdown();
   ecore_con_shutdown();
   ecore_shutdown();
   eina_shutdown();
   return 0;
}
#endif
