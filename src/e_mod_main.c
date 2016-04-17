#include <e.h>
#include <Ecore.h>
#include <Ecore_Con.h>
#include "e_mod_main.h"

typedef struct _Instance Instance;

static char but_str[1000000] = "";

static char *session_id = NULL;

struct _Instance
{
   E_Gadcon_Client *gcc;
   Ecore_Timer *timer;
   Config_Item *ci;
   E_Gadcon_Popup *popup;
   Evas_Object *o_icon;
};

static E_Config_DD *conf_edd = NULL;
static E_Config_DD *conf_item_edd = NULL;

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
             elm_object_text_set(button, "Popup");
             elm_box_pack_end(items_box, button);
             evas_object_show(button);

             button = elm_button_add(items_box);
             evas_object_size_hint_align_set(button, EVAS_HINT_FILL, EVAS_HINT_FILL);
             evas_object_size_hint_weight_set(button, EVAS_HINT_EXPAND, 0.0);
             elm_object_text_set(button, but_str);
             elm_box_pack_end(items_box, button);
             evas_object_show(button);

             evas_object_show(items_box);

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

   cpu_conf->instances = eina_list_append(cpu_conf->instances, inst);

   evas_object_event_callback_add(inst->o_icon, EVAS_CALLBACK_MOUSE_DOWN,
				  _button_cb_mouse_down, inst);

//   inst->timer = ecore_timer_add(inst->ci->interval, _set_cpu_load, inst);
   return gcc;
}

static void
_gc_shutdown(E_Gadcon_Client *gcc)
{
   Instance *inst;

   inst = gcc->data;

   if (inst->timer) ecore_timer_del(inst->timer);
   if (inst->o_icon) evas_object_del(inst->o_icon);

   cpu_conf->instances = eina_list_remove(cpu_conf->instances, inst);
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

static const char *baseUrl = "http://127.0.0.1:9091/transmission/rpc";

static Eina_Bool
_url_complete_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event_info)
{
   Ecore_Con_Event_Url_Complete *url_complete = event_info;

   if (url_complete->status)
     {
        if (!session_id)
          {
             const Eina_List *hdrs = ecore_con_url_response_headers_get(url_complete->url_con), *itr;
             char *hdr;
             EINA_LIST_FOREACH(hdrs, itr, hdr)
               {
                  if (strstr(hdr, "X-Transmission-Session-Id"))
                    {
                       char *tmp;
                       session_id = strdup(hdr + 27);
                       tmp = session_id;
                       while (*tmp)
                         {
                            if (*tmp == 0x0D) *tmp = '\0';
                            else tmp++;
                         }
                    }
               }
             sprintf(but_str, "%s", session_id);
          }
     }
   else
     {
        sprintf(but_str, "Error %d", url_complete->status);
        free(session_id);
        session_id = NULL;
     }


   return EINA_TRUE;
}

static Eina_Bool
_session_id_poller_cb(void *data EINA_UNUSED)
{
   Ecore_Con_Url *ec_url = NULL;

   ec_url = ecore_con_url_new(baseUrl);
   if (!ec_url) return EINA_FALSE;

   ecore_con_url_proxy_set(ec_url, NULL);
   ecore_event_handler_add(ECORE_CON_EVENT_URL_COMPLETE, _url_complete_cb, NULL);

   ecore_con_url_get(ec_url);
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

   ecore_timer_add(5.0, _session_id_poller_cb, NULL);
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
