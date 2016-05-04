#include <e.h>
#include <Ecore.h>
#include <Ecore_Con.h>
#include "e_mod_main.h"

#define IP_ADDR "127.0.0.1"

static const char *baseUrl = "http://%s:9091/transmission/rpc";
static void *_url_session_id_data_test = (void *)0;
static void *_url_torrents_data_test = (void *)1;

typedef struct
{
   const char *buffer;
   const char *current;
   unsigned int line_no;
   unsigned int offset;
} Lexer;

typedef struct
{
   const char *name;
   Eo *name_label;
} Item_Desc;

typedef struct
{
   E_Gadcon_Client *gcc;
   Ecore_Timer *timer;
   Config_Item *ci;
   E_Gadcon_Popup *popup;
   Evas_Object *o_icon;
   Eo *items_box;
   Eina_List *items_list;
   char but1_str[1000000]; // TEMP
   char but2_str[1000000]; // TEMP
   char *session_id;
   char *torrents_data_buf;
   int torrents_data_buf_len;
   int torrents_data_len;
} Instance;

static E_Config_DD *conf_edd = NULL;
static E_Config_DD *conf_item_edd = NULL;

static Eina_List *instances = NULL;

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

static void
_box_update(Instance *inst)
{
   Eina_List *itr;
   Item_Desc *d;
   EINA_LIST_FOREACH(inst->items_list, itr, d)
     {
        if (!d->name_label)
          {
             Eo *label = elm_label_add(inst->items_box);
             evas_object_size_hint_align_set(label, 0.0, EVAS_HINT_FILL);
             evas_object_size_hint_weight_set(label, EVAS_HINT_EXPAND, 0.0);
             elm_object_text_set(label, d->name);
             elm_box_pack_end(inst->items_box, label);
             evas_object_show(label);
             eo_wref_add(label, &d->name_label);
          }
     }
}

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
             Evas_Object *items_box, *button;
             inst->popup = e_gadcon_popup_new(inst->gcc, 0);

             items_box = elm_box_add(e_comp->elm);
             evas_object_size_hint_align_set(items_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
             evas_object_size_hint_weight_set(items_box, EVAS_HINT_EXPAND, 0.0);

             button = elm_button_add(items_box);
             evas_object_size_hint_align_set(button, EVAS_HINT_FILL, EVAS_HINT_FILL);
             evas_object_size_hint_weight_set(button, EVAS_HINT_EXPAND, 0.0);
             elm_object_text_set(button, inst->but1_str);
             elm_box_pack_end(items_box, button);
             evas_object_show(button);

             button = elm_button_add(items_box);
             evas_object_size_hint_align_set(button, EVAS_HINT_FILL, EVAS_HINT_FILL);
             evas_object_size_hint_weight_set(button, EVAS_HINT_EXPAND, 0.0);
             elm_object_text_set(button, inst->but2_str);
             elm_box_pack_end(items_box, button);
             evas_object_show(button);

             evas_object_show(items_box);
             eo_wref_add(items_box, &inst->items_box);
             _box_update(inst);

             e_gadcon_popup_content_set(inst->popup, items_box);
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

static E_Gadcon_Client *
_gc_init(E_Gadcon *gc, const char *name, const char *id, const char *style)
{
   Instance *inst;
   E_Gadcon_Client *gcc;
   char buf[4096];

   inst = E_NEW(Instance, 1);

   snprintf(buf, sizeof(buf), "%s/transmission.edj", e_module_dir_get(cpu_conf->module));

   inst->o_icon = edje_object_add(gc->evas);
   if (!e_theme_edje_object_set(inst->o_icon,
				"base/theme/modules/transmission", "modules/transmission/main"))
      edje_object_file_set(inst->o_icon, buf, "modules/transmission/main");
   evas_object_show(inst->o_icon);

   gcc = e_gadcon_client_new(gc, name, id, style, inst->o_icon);
   gcc->data = inst;
   inst->gcc = gcc;
   instances = eina_list_append(instances, inst);

   evas_object_event_callback_add(inst->o_icon, EVAS_CALLBACK_MOUSE_DOWN,
				  _button_cb_mouse_down, inst);

   return gcc;
}

static void
_gc_shutdown(E_Gadcon_Client *gcc)
{
   Instance *inst;

   inst = gcc->data;

   if (inst->timer) ecore_timer_del(inst->timer);
   if (inst->o_icon) evas_object_del(inst->o_icon);

   instances = eina_list_remove(instances, inst);
   E_FREE(inst);
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

#if 0
static void
_cpu_menu_fast(void *data, E_Menu *m EINA_UNUSED, E_Menu_Item *mi EINA_UNUSED)
{
   Instance *inst;

   inst = data;
   inst->ci->interval = 0.5;
   ecore_timer_del(inst->timer);
   inst->timer = ecore_timer_add(inst->ci->interval, _set_cpu_load, inst);
   e_config_save_queue();
}
#endif

static void
_lexer_reset(Lexer *l)
{
   l->current = l->buffer;
   l->line_no = l->offset = 0;
}

static void
_ws_skip(Lexer *l)
{
   /*
    * Skip spaces and \n
    * For \n, inc line_no and reset offset
    * otherwise inc offset
    */
   do
     {
        char c = *(l->current);
        switch (c)
          {
           case ' ':
              l->offset++;
              break;
           case '\n':
              l->line_no++;
              l->offset = 0;
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
        l->offset += strlen(token);
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
   l->offset += size;
   return word;
}

static int
_next_number(Lexer *l)
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
   l->offset += size;
   return atoi(n_str);
}

static Eina_Bool
_jump_at(Lexer *l, const char *token, Eina_Bool over)
{
   char *found = strstr(l->current, token);
   if (!found) return EINA_FALSE;
   l->current = over ? found + strlen(token) : found;
   l->offset = l->current - l->buffer;
   return EINA_TRUE;
}

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

static Eina_Bool
_session_id_get_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event_info)
{
   Ecore_Con_Event_Url_Complete *url_complete = event_info;
   Ecore_Con_Url *ec_url = url_complete->url_con;
   Instance *inst = eo_key_data_get(ec_url, "Transmission_Instance");
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
          }
        sprintf(inst->but1_str, "ZZZ%sZZZ", inst->session_id);
     }
   else
     {
        sprintf(inst->but1_str, "Error %d", url_complete->status);
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
        sprintf(url, baseUrl, IP_ADDR);
        Ecore_Con_Url *ec_url = ecore_con_url_new(url);
        if (!ec_url) return EINA_TRUE;
        ecore_con_url_proxy_set(ec_url, NULL);
        ecore_con_url_data_set(ec_url, &_url_session_id_data_test);
        eo_key_data_set(ec_url, "Transmission_Instance", inst);
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
                  int leftuntildone = 0;
                  if (!_is_next_token(&l, "{")) return EINA_FALSE;
                  while (!_is_next_token(&l, "}"))
                    {
                       if (_is_next_token(&l, "\"name\":\""))
                         {
                            name = _next_word(&l, ".[]_- ", EINA_TRUE);
                            _jump_at(&l, ",", EINA_TRUE);
                         }
                       else if (_is_next_token(&l, "\"leftUntilDone\":"))
                         {
                            leftuntildone = _next_number(&l);
                            _jump_at(&l, ",", EINA_TRUE);
                         }
                       else _jump_at(&l, "}", EINA_FALSE);
                    }
                  if (name)
                    {
                       Item_Desc *d = _item_find_by_name(inst->items_list, name);
                       if (!d)
                         {
                            d = E_NEW(Item_Desc, 1);
                            inst->items_list = eina_list_append(inst->items_list, d);
                            d->name = eina_stringshare_add(name);
                         }
                       free(name);
                    }
                  _is_next_token(&l, ",");
               }
          }
     }
   return EINA_TRUE;
}

static Eina_Bool
_torrents_data_get_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event_info)
{
   Ecore_Con_Event_Url_Data *url_data = event_info;
   Ecore_Con_Url *ec_url = url_data->url_con;
   Instance *inst = eo_key_data_get(ec_url, "Transmission_Instance");
   void **test = ecore_con_url_data_get(ec_url);
   if (!inst || !test || *test != _url_torrents_data_test) return EINA_TRUE;
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

   if (url_complete->status)
     {
        Ecore_Con_Url *ec_url = url_complete->url_con;
        Instance *inst = eo_key_data_get(ec_url, "Transmission_Instance");
        void **test = ecore_con_url_data_get(ec_url);
        if (!inst || !inst->torrents_data_len ||
              !test || *test != _url_torrents_data_test) return EINA_TRUE;
          {
             _json_data_parse(inst);
             _box_update(inst);
          }
        inst->torrents_data_len = 0;
     }
   return EINA_FALSE;
}

static Eina_Bool
_torrents_poller_cb(void *data EINA_UNUSED)
{
   Eina_List *itr;
   Instance *inst;
   const char *fields_list = "{\"arguments\":{\"fields\":[\"name\", \"leftUntilDone\", \"rateDownload\", \"rateUpload\", \"sizeWhenDone\", \"uploadRatio\"]}, \"method\":\"torrent-get\"}";
   int len = strlen(fields_list);
   EINA_LIST_FOREACH(instances, itr, inst)
     {
        char url[1024];
        if (!inst->session_id) continue;
        sprintf(url, baseUrl, IP_ADDR);
        Ecore_Con_Url *ec_url = ecore_con_url_new(url);
        if (!ec_url) continue;
        ecore_con_url_proxy_set(ec_url, NULL);
        ecore_con_url_data_set(ec_url, &_url_torrents_data_test);
        eo_key_data_set(ec_url, "Transmission_Instance", inst);

        ecore_con_url_additional_header_add(ec_url, "X-Transmission-Session-Id", inst->session_id);
        //ecore_con_url_additional_header_add(ec_url, "Content-Length", len);

        ecore_con_url_post(ec_url, fields_list, len, NULL);
     }
   return EINA_TRUE;
}

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
