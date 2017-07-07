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

#define _EET_ENTRY "config"

static const char *baseUrl = "http://%s:9091/transmission/rpc";

typedef struct
{
   const char *buffer;
   const char *current;
} Lexer;

typedef struct
{
   const char *hostname;
   const char *torrents_dir;
   const char *download_server;
   const char *remote_dir;
} Server_Config;

typedef struct
{
   Eina_List *servers_cfgs;
} Config;

typedef struct
{
#ifndef STAND_ALONE
   E_Gadcon_Client *gcc;
   E_Gadcon_Popup *popup;
#endif
   Ecore_Timer *session_id_timer;
   Ecore_Timer *torrents_poller_timer;
   Evas_Object *o_icon;
   Eo *main_box, *items_table, *no_conn_label, *error_label;
   Eina_List *items_list;
   char *session_id;
   char *torrents_data_buf;
   char *last_error;
   unsigned int torrents_data_buf_len;
   unsigned int torrents_data_len;

   const Server_Config *scfg;
   Ecore_File_Monitor *torrents_dir_monitor;
} Instance;

typedef struct
{
   char *full_name;
   unsigned int cur_len;
   unsigned int total_len;
} File_Info;

typedef struct
{
   Instance *inst;
   const char *name;
   unsigned long size;
   unsigned int downrate, uprate, id, status;
   double done, ratio;
   Eina_List *files; /* List of File_Info */

   int table_idx;
   Eo *name_label;
   Eo *size_label;
   Eo *done_label;
   Eo *downrate_label, *uprate_label, *ratio_label;
   Eo *start_button, *start_icon, *pause_icon;
   Eo *del_button, *del_icon;
   Eo *download_button, *download_icon;

   Eo *download_exe;

   Eina_Bool valid : 1;
} Item_Desc;

static Eet_Data_Descriptor *_config_edd = NULL;

static Config *_config = NULL;

#ifndef STAND_ALONE
static E_Module *_module = NULL;
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
   DOWNLOAD_COL,
   DEL_COL,
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

static void
_can_read_changed(void *data EINA_UNUSED, const Efl_Event *ev)
{
   static int max_size = 16384;
   Eina_Rw_Slice slice;
   Eo *dialer = ev->object;
   Instance *inst = efl_key_data_get(dialer, "Transmission_Instance");
   if (efl_key_data_get(dialer, "can_read_changed")) return;
   efl_key_data_set(dialer, "can_read_changed", dialer);

   slice.mem = malloc(max_size);
   slice.len = max_size;

   while (efl_io_reader_can_read_get(dialer))
     {
        if (efl_io_reader_read(dialer, &slice)) goto ret;
        if (slice.len > (inst->torrents_data_buf_len - inst->torrents_data_len))
          {
             inst->torrents_data_buf_len = inst->torrents_data_len + slice.len;
             inst->torrents_data_buf = realloc(inst->torrents_data_buf,
                   inst->torrents_data_buf_len + 1);
          }
        memcpy(inst->torrents_data_buf + inst->torrents_data_len,
              slice.mem, slice.len);
        inst->torrents_data_len += slice.len;
        inst->torrents_data_buf[inst->torrents_data_len] = '\0';
        slice.len = max_size;
     }
ret:
   free(slice.mem);
   efl_key_data_set(dialer, "can_read_changed", NULL);
}

static void
_dialer_delete(void *data EINA_UNUSED, const Efl_Event *ev)
{
   Eo *dialer = ev->object;
   efl_del(efl_key_data_get(dialer, "post-buffer"));
   efl_del(efl_key_data_get(dialer, "copier-buffer-dialer"));
   efl_del(dialer);
}

static Efl_Net_Dialer_Http *
_dialer_create(Eina_Bool is_get_method, const char *data, Efl_Event_Cb cb)
{
   Eo *dialer = efl_add(EFL_NET_DIALER_HTTP_CLASS, ecore_main_loop_get(),
         efl_net_dialer_http_method_set(efl_added, is_get_method?"GET":"POST"),
         efl_net_dialer_proxy_set(efl_added, NULL),
         efl_net_dialer_http_request_header_add(efl_added, "Accept-Encoding", "identity"),
         efl_event_callback_add(efl_added, EFL_IO_READER_EVENT_CAN_READ_CHANGED, _can_read_changed, NULL));
   if (cb)
      efl_event_callback_add(dialer, EFL_IO_READER_EVENT_EOS, cb, NULL);
   efl_event_callback_add(dialer, EFL_IO_READER_EVENT_EOS, _dialer_delete, NULL);

   if (!is_get_method && data)
     {
        Eina_Slice slice = { .mem = data, .len = strlen(data) };
        Eo *buffer = efl_add(EFL_IO_BUFFER_CLASS, efl_loop_get(dialer),
              efl_name_set(efl_added, "post-buffer"),
              efl_io_closer_close_on_destructor_set(efl_added, EINA_TRUE),
              efl_io_closer_close_on_exec_set(efl_added, EINA_TRUE));
        efl_io_writer_write(buffer, &slice, NULL);
        efl_key_data_set(dialer, "post-buffer", buffer);

        Eo *copier = efl_add(EFL_IO_COPIER_CLASS, efl_loop_get(dialer),
              efl_name_set(efl_added, "copier-buffer-dialer"),
              efl_io_copier_source_set(efl_added, buffer),
              efl_io_copier_destination_set(efl_added, dialer),
              efl_io_closer_close_on_destructor_set(efl_added, EINA_FALSE));
        efl_key_data_set(dialer, "copier-buffer-dialer", copier);
     }

   return dialer;
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
   sprintf(url, baseUrl, inst->scfg->hostname);
   Efl_Net_Dialer_Http *dialer = _dialer_create(EINA_FALSE, request, NULL);
   efl_net_dialer_http_request_header_add(dialer, "X-Transmission-Session-Id", inst->session_id);
   efl_key_data_set(dialer, "Transmission_Instance", inst);
   efl_net_dialer_dial(dialer, url);
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
         "\"arguments\":{\"ids\":[%d],\"delete-local-data\":true}}",
         d->id);
   sprintf(url, baseUrl, inst->scfg->hostname);
   Efl_Net_Dialer_Http *dialer = _dialer_create(EINA_FALSE, request, NULL);
   efl_net_dialer_http_request_header_add(dialer, "X-Transmission-Session-Id", inst->session_id);
   efl_key_data_set(dialer, "Transmission_Instance", inst);
   efl_net_dialer_dial(dialer, url);
}

static void
_download_tooltip_show(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   elm_object_tooltip_show(obj);
}

static void
_download_tooltip_hide(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   elm_object_tooltip_hide(obj);
}

static void
_tooltip_enable(Eo *obj, Eina_Bool enable)
{
   elm_object_disabled_set(obj, enable);
   if (enable)
     {
        evas_object_event_callback_add(obj, EVAS_CALLBACK_MOUSE_IN, _download_tooltip_show, NULL);
        evas_object_event_callback_add(obj, EVAS_CALLBACK_MOUSE_OUT, _download_tooltip_hide, NULL);
     }
   else
     {
        elm_object_tooltip_text_set(obj, NULL);
        elm_object_tooltip_hide(obj);
        evas_object_event_callback_del_full(obj, EVAS_CALLBACK_MOUSE_IN, _download_tooltip_show, NULL);
        evas_object_event_callback_del_full(obj, EVAS_CALLBACK_MOUSE_OUT, _download_tooltip_hide, NULL);
     }
}

static Eina_Bool
_rsync_end_cb(void *data, int type EINA_UNUSED, void *event)
{
   Ecore_Exe_Event_Del *event_info = (Ecore_Exe_Event_Del *)event;
   Ecore_Exe *exe = event_info->exe;
   Item_Desc *d = ecore_exe_data_get(exe);
   if (!d || d->inst != data) return ECORE_CALLBACK_PASS_ON;
   _tooltip_enable(d->download_button, EINA_FALSE);
   return ECORE_CALLBACK_DONE;
}

static Eina_Bool
_rsync_output_cb(void *data, int type EINA_UNUSED, void *event)
{
   Ecore_Exe_Event_Data *event_data = (Ecore_Exe_Event_Data *)event;
   const char *begin = event_data->data;
   Ecore_Exe *exe = event_data->exe;
   Item_Desc *d = ecore_exe_data_get(exe);
   if (!d || d->inst != data) return ECORE_CALLBACK_PASS_ON;

   while (*begin == 0xd || *begin == ' ') begin++;
   elm_object_tooltip_text_set(d->download_button, begin);

   return ECORE_CALLBACK_DONE;
}

static void
_download_bt_clicked(void *data, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   Item_Desc *d = data;
   Instance *inst = d->inst;
   if (!inst->session_id) return;
   if (eina_list_count(d->files) == 1)
     {
        char cmd[1024];
        File_Info *info = eina_list_data_get(d->files);
        sprintf(cmd, "rsync -avzhP --protect-args --inplace %s:%s/\"%s\" /home/daniel/Desktop/Downloads",
              inst->scfg->download_server, inst->scfg->remote_dir, info->full_name);
        d->download_exe = ecore_exe_pipe_run(cmd, ECORE_EXE_PIPE_READ | ECORE_EXE_PIPE_ERROR, d);
        efl_wref_add(d->download_exe, &(d->download_exe));
        elm_object_tooltip_text_set(obj, "");
        elm_object_tooltip_show(obj);
        _tooltip_enable(obj, EINA_TRUE);
        printf("Download %s\n", info->full_name);
     }
}

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

#if 0
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
#endif

static long
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
   return atol(n_str);
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

static void
_box_update(Instance *inst, Eina_Bool clear)
{
   char str[128];;
   Eina_List *itr, *itr2;
   Item_Desc *d, *d2;

   if (!inst->main_box) return;

   if (clear)
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
     }

   EINA_LIST_FOREACH(inst->items_list, itr, d)
     {
        if (!d->valid)
          {
             int i;
             for (i = NAME_COL; i <= DEL_COL; i++)
                efl_del(elm_table_child_get(inst->items_table, i, d->table_idx));
             d->table_idx = 0;
             continue;
          }
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

        _icon_create(inst->items_table, "go-down", &d->download_icon);
        _button_create(inst->items_table, NULL, d->download_icon, &d->download_button, _download_bt_clicked, d);
        elm_table_pack(inst->items_table, d->download_button, DOWNLOAD_COL, d->table_idx, 1, 1);
        if (d->download_exe) _tooltip_enable(d->download_button, EINA_TRUE);

        _icon_create(inst->items_table, "application-exit", &d->del_icon);
        _button_create(inst->items_table, NULL, d->del_icon, &d->del_button, _del_bt_clicked, d);
        elm_table_pack(inst->items_table, d->del_button, DEL_COL, d->table_idx, 1, 1);
     }
   EINA_LIST_FOREACH_SAFE(inst->items_list, itr, itr2, d)
     {
        if (d->valid) continue;
        inst->items_list = eina_list_remove(inst->items_list, d);
        free(d);
     }
}

static void
_torrent_added_cb(void *data EINA_UNUSED, const Efl_Event *ev);

static Eina_Bool
_torrent_add(Instance *inst, const char *file)
{
   char url[1024], full_path[256];
   char *content = NULL, *ret_content = NULL, *request = NULL;
   FILE *fp = NULL;
   int filesize, retsize;
   Eina_Bool ret = EINA_FALSE;
   if (!inst->session_id) goto end;

   sprintf(full_path, "%s/%s", inst->scfg->torrents_dir, file);
   fp = fopen(full_path, "rb");
   if (!fp)
     {
        printf("Can't open file %s\n", full_path);
        goto end;
     }
   fseek(fp, 0, SEEK_END);
   filesize = ftell(fp);
   if (filesize < 0) goto end;
   fseek(fp, 0, SEEK_SET);
   content = malloc(filesize + 1);
   if (fread(content, filesize, 1, fp) != 1) goto end;
   content[filesize] = '\0';

   ret_content = base64_encode(content, filesize, &retsize);
   request = malloc(retsize + 256);
   sprintf(request,
         "{\"method\":\"torrent-add\", "
         "\"arguments\":{\"metainfo\":\"%s\"}}", ret_content);
   sprintf(url, baseUrl, inst->scfg->hostname);
   Efl_Net_Dialer_Http *dialer = _dialer_create(EINA_FALSE, request, _torrent_added_cb);
   efl_net_dialer_http_request_header_add(dialer, "X-Transmission-Session-Id", inst->session_id);
   efl_key_data_set(dialer, "Transmission_Instance", inst);
   efl_key_data_set(dialer, "Transmission_FileToRemove", eina_stringshare_add(full_path));
   efl_net_dialer_dial(dialer, url);
   ret = EINA_TRUE;
end:
   if (fp) fclose(fp);
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
   Eina_List *l = ecore_file_ls(inst->scfg->torrents_dir);
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
   inst->torrents_data_len = 0;
}

static Instance *
_instance_create(const Server_Config *scfg)
{
   Instance *inst = calloc(1, sizeof(Instance));
   inst->scfg = scfg;
   inst->torrents_dir_monitor = ecore_file_monitor_add(inst->scfg->torrents_dir,
         _torrents_dir_changed, inst);
   return inst;
}

static void
_config_eet_load()
{
   Eet_Data_Descriptor *srv_edd;
   if (_config_edd) return;
   Eet_Data_Descriptor_Class eddc;

   EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Server_Config);
   srv_edd = eet_data_descriptor_stream_new(&eddc);
   EET_DATA_DESCRIPTOR_ADD_BASIC(srv_edd, Server_Config, "hostname", hostname, EET_T_STRING);
   EET_DATA_DESCRIPTOR_ADD_BASIC(srv_edd, Server_Config, "torrents_dir", torrents_dir, EET_T_STRING);
   EET_DATA_DESCRIPTOR_ADD_BASIC(srv_edd, Server_Config, "download_server", download_server, EET_T_STRING);
   EET_DATA_DESCRIPTOR_ADD_BASIC(srv_edd, Server_Config, "remote_dir", remote_dir, EET_T_STRING);

   EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Config);
   _config_edd = eet_data_descriptor_stream_new(&eddc);

   EET_DATA_DESCRIPTOR_ADD_LIST(_config_edd, Config, "servers_cfgs", servers_cfgs, srv_edd);
}

static void
_config_save()
{
   char path[1024];
   sprintf(path, "%s/e_transmission/config", efreet_config_home_get());
   _config_eet_load();
   Eet_File *file = eet_open(path, EET_FILE_MODE_WRITE);
   eet_data_write(file, _config_edd, _EET_ENTRY, _config, EINA_TRUE);
   eet_close(file);
}

static Eina_Bool
_mkdir(const char *dir)
{
   if (!ecore_file_exists(dir))
     {
        Eina_Bool success = ecore_file_mkdir(dir);
        if (!success)
          {
             printf("Cannot create a config folder \"%s\"\n", dir);
             return EINA_FALSE;
          }
     }
   return EINA_TRUE;
}

static void
_config_load()
{
   char path[1024];

   sprintf(path, "%s/e_transmission", efreet_config_home_get());
   if (!_mkdir(path)) return;

   sprintf(path, "%s/e_transmission/config", efreet_config_home_get());
   _config_eet_load();
   Eet_File *file = eet_open(path, EET_FILE_MODE_READ);
   if (!file)
     {
        _config = calloc(1, sizeof(Config));
        Server_Config *scfg = calloc(1, sizeof(*scfg));
        scfg->hostname = "127.0.0.1";
        scfg->torrents_dir = "/home/daniel/Downloads";
        scfg->download_server = "127.0.0.1";
        scfg->remote_dir = "/home/daniel/Downloads";
        _config->servers_cfgs = eina_list_append(_config->servers_cfgs, scfg);
     }
   else
     {
        _config = eet_data_read(file, _config_edd, _EET_ENTRY);
        eet_close(file);
     }

   _config_save();
}

static void
_session_id_get_cb(void *data EINA_UNUSED, const Efl_Event *ev)
{
//   printf("TRANS: In - %s\n", __FUNCTION__);
   Efl_Net_Dialer_Http *dialer = ev->object;
   Instance *inst = efl_key_data_get(dialer, "Transmission_Instance");

   if (inst->torrents_data_len)
     {
        char *id = strstr(inst->torrents_data_buf, "X-Transmission-Session-Id: ");
        if (id)
          {
             char *end = strchr(id, '<');
             *end = '\0';
             id = strchr(id, ' ') + 1;
             if (!inst->session_id || strcmp(inst->session_id, id))
               {
                  free(inst->session_id);
                  inst->session_id = strdup(id);
                  _torrents_dir_changed(inst, NULL, ECORE_FILE_EVENT_MODIFIED, NULL);
                  printf("New Id: %s\n", inst->session_id);
               }
          }
     }
   else
     {
        if (inst->session_id)
          {
             free(inst->session_id);
             inst->session_id = NULL;
          }
     }
   inst->torrents_data_len = 0;
}

static Eina_Bool
_session_id_poller_cb(void *data)
{
   Instance *inst = data;
//   printf("TRANS: In - %s\n", __FUNCTION__);
   char url[1024];
   sprintf(url, baseUrl, inst->scfg->hostname);
   Efl_Net_Dialer_Http *dialer = _dialer_create(EINA_TRUE, NULL, _session_id_get_cb);
   efl_key_data_set(dialer, "Transmission_Instance", inst);
   efl_net_dialer_dial(dialer, url);
   return EINA_TRUE;
}

static void
_instance_delete(Instance *inst)
{
   ecore_timer_del(inst->session_id_timer);
   ecore_timer_del(inst->torrents_poller_timer);
   if (inst->o_icon) evas_object_del(inst->o_icon);
   if (inst->main_box) evas_object_del(inst->main_box);

   free(inst);
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
   Eina_List *itr;
   Item_Desc *d;
   Lexer l;
   l.buffer = inst->torrents_data_buf;
   _lexer_reset(&l);
   if (!_is_next_token(&l, "{")) return EINA_FALSE;
   EINA_LIST_FOREACH(inst->items_list, itr, d) d->valid = EINA_FALSE;
   if (_is_next_token(&l, "\"arguments\":{"))
     {
        if (_is_next_token(&l, "\"torrents\":["))
          {
             while (!_is_next_token(&l, "]"))
               {
                  char *name = NULL;
                  Eina_List *files_info = NULL;
                  long leftuntildone = 0, id = 0, status = 0, uprate = 0, downrate = 0;
                  unsigned long size = 0;
                  double ratio = 0.0;
                  if (!_is_next_token(&l, "{")) return EINA_FALSE;
                  while (!_is_next_token(&l, "}"))
                    {
                       if (_is_next_token(&l, "\"name\":\""))
                         {
                            const char *begin = l.current;
                            JUMP_AT(&l, "\"", EINA_FALSE);
                            name = malloc(l.current - begin + 1);
                            memcpy(name, begin, l.current - begin);
                            name[l.current - begin] = '\0';
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
                       else if (_is_next_token(&l, "\"files\":["))
                         {
                            while (!_is_next_token(&l, "]"))
                              {
                                 if (!_is_next_token(&l, "{")) return EINA_FALSE;
                                 unsigned int cur = 0, total = 0;
                                 char *filename = NULL;
                                 File_Info *info;
                                 while (!_is_next_token(&l, "}"))
                                   {
                                      if (_is_next_token(&l, "\"bytesCompleted\":"))
                                        {
                                           cur = _next_integer(&l);
                                           JUMP_AT(&l, ",", EINA_TRUE, "}", EINA_FALSE);
                                        }
                                      else if (_is_next_token(&l, "\"length\":"))
                                        {
                                           total = _next_integer(&l);
                                           JUMP_AT(&l, ",", EINA_TRUE, "}", EINA_FALSE);
                                        }
                                      else if (_is_next_token(&l, "\"name\":\""))
                                        {
                                           const char *begin = l.current;
                                           JUMP_AT(&l, "\"", EINA_FALSE);
                                           filename = malloc(l.current - begin + 1);
                                           memcpy(filename, begin, l.current - begin);
                                           filename[l.current - begin] = '\0';
                                           JUMP_AT(&l, ",", EINA_TRUE, "}", EINA_FALSE);
                                        }
                                   }
                                 info = malloc(sizeof(*info));
                                 info->cur_len = cur;
                                 info->total_len = total;
                                 info->full_name = filename;
                                 files_info = eina_list_append(files_info, info);
                                 _is_next_token(&l, ",");
                              }
                            JUMP_AT(&l, ",", EINA_TRUE, "}", EINA_FALSE);
                         }
                       else
                         {
                            if (!JUMP_AT(&l, "}", EINA_FALSE)) return EINA_FALSE;
                         }
                    }
                  if (id)
                    {
                       d = _item_find_by_name(inst->items_list, name);
                       File_Info *info;
                       if (!d)
                         {
                            printf("New item\n");
                            d = calloc(1, sizeof(Item_Desc));
                            d->inst = inst;
                            d->id = id;
                            d->name = eina_stringshare_add(name);
                            inst->items_list = eina_list_append(inst->items_list, d);
                         }
                       EINA_LIST_FREE(d->files, info)
                         {
                            free(info->full_name);
                            free(info);
                         }
                       d->valid = EINA_TRUE;
                       d->size = size;
                       d->downrate = downrate;
                       d->uprate = uprate;
                       d->ratio = ratio;
                       d->status = status;
                       d->done = 100.0 - (100 * ((double)leftuntildone / size));
                       d->files = files_info;
                       free(name);
                    }
                  _is_next_token(&l, ",");
               }
          }
     }
   return EINA_TRUE;
}

static void
_torrents_stats_get_cb(void *data EINA_UNUSED, const Efl_Event *ev)
{
   Efl_Net_Dialer_Http *dialer = ev->object;
   Instance *inst = efl_key_data_get(dialer, "Transmission_Instance");
   char *result_str = strstr(inst->torrents_data_buf, "\"result\":");
//   printf("TRANS: In - %s\n", __FUNCTION__);
   if (!result_str) return;
   if (strncmp(result_str + strlen("\"result\":"), "\"success\"", 9))
     {
        if (inst->last_error) free(inst->last_error);
        char *end = strchr(result_str + strlen("\"result\":\""), '\"');
        inst->last_error = malloc(end - result_str + 1);
        strncpy(inst->last_error, result_str, end - result_str);
     }
   else
     {
        _json_data_parse(inst);
        _box_update(inst, EINA_FALSE);
     }
   inst->torrents_data_len = 0;
}

static Eina_Bool
_torrents_poller_cb(void *data)
{
   Instance *inst = data;
   const char *fields_list = "{\"arguments\":{\"fields\":[\"name\", \"status\", \"id\", \"leftUntilDone\", \"rateDownload\", \"rateUpload\", \"sizeWhenDone\", \"uploadRatio\", \"files\"]}, \"method\":\"torrent-get\"}";
//   printf("TRANS: In - %s\n", __FUNCTION__);
   char url[1024];
   if (!inst->session_id)
     {
        _box_update(inst, EINA_TRUE);
        return EINA_TRUE;
     }
   sprintf(url, baseUrl, inst->scfg->hostname);
   Efl_Net_Dialer_Http *dialer = _dialer_create(EINA_FALSE, fields_list, _torrents_stats_get_cb);
   efl_net_dialer_http_request_header_add(dialer, "X-Transmission-Session-Id", inst->session_id);
   efl_key_data_set(dialer, "Transmission_Instance", inst);
   efl_net_dialer_dial(dialer, url);
   return EINA_TRUE;
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
//   printf("TRANS: In - %s\n", __FUNCTION__);
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

             _box_update(inst, EINA_FALSE);

             e_gadcon_popup_content_set(inst->popup, inst->main_box);
             e_comp_object_util_autoclose(inst->popup->comp_object,
                   _popup_comp_del_cb, NULL, inst);
             e_gadcon_popup_show(inst->popup);
             e_object_data_set(E_OBJECT(inst->popup), inst);
             E_OBJECT_DEL_SET(inst->popup, _popup_del_cb);
          }
     }
}

static E_Gadcon_Client *
_gc_init(E_Gadcon *gc, const char *name, const char *id, const char *style)
{
   Server_Config *scfg;
   Instance *inst;
   E_Gadcon_Client *gcc;
   char buf[4096];

//   printf("TRANS: In - %s\n", __FUNCTION__);

   scfg = eina_list_data_get(_config->servers_cfgs);
   inst = _instance_create(scfg);

   snprintf(buf, sizeof(buf), "%s/transmission.edj", e_module_dir_get(_module));

   inst->o_icon = edje_object_add(gc->evas);
   if (!e_theme_edje_object_set(inst->o_icon,
				"base/theme/modules/transmission",
                                "modules/transmission/main"))
      edje_object_file_set(inst->o_icon, buf, "modules/transmission/main");
   evas_object_show(inst->o_icon);

   gcc = e_gadcon_client_new(gc, name, id, style, inst->o_icon);
   gcc->data = inst;
   inst->gcc = gcc;

   inst->session_id_timer = ecore_timer_add(1.0, _session_id_poller_cb, inst);
   inst->torrents_poller_timer = ecore_timer_add(1.0, _torrents_poller_cb, inst);
   _session_id_poller_cb(inst);

   evas_object_event_callback_add(inst->o_icon, EVAS_CALLBACK_MOUSE_DOWN,
				  _button_cb_mouse_down, inst);

   ecore_event_handler_add(ECORE_EXE_EVENT_DATA, _rsync_output_cb, inst);
   ecore_event_handler_add(ECORE_EXE_EVENT_DEL, _rsync_end_cb, inst);

   return gcc;
}

static void
_gc_shutdown(E_Gadcon_Client *gcc)
{
//   printf("TRANS: In - %s\n", __FUNCTION__);
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

   if (!_module) return NULL;

   snprintf(buf, sizeof(buf), "%s/e-module-transmission.edj", e_module_dir_get(_module));

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

EAPI void *
e_modapi_init(E_Module *m)
{
//   printf("TRANS: In - %s\n", __FUNCTION__);
   ecore_init();
   ecore_con_init();
   ecore_con_url_init();
   efreet_init();

   _module = m;
   _config_load();
   e_gadcon_provider_register(&_gc_class);

   return m;
}

EAPI int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
//   printf("TRANS: In - %s\n", __FUNCTION__);
   e_gadcon_provider_unregister(&_gc_class);

   _module = NULL;
   efreet_shutdown();
   ecore_con_url_shutdown();
   ecore_con_shutdown();
   ecore_shutdown();
   return 1;
}

EAPI int
e_modapi_save(E_Module *m EINA_UNUSED)
{
   //e_config_domain_save("module.transmission", conf_edd, cpu_conf);
   return 1;
}
#else
int main(int argc, char **argv)
{
   Server_Config *scfg;
   Instance *inst;

   eina_init();
   ecore_init();
   ecore_con_init();
   efreet_init();
   elm_init(argc, argv);

   _config_load();
   scfg = eina_list_data_get(_config->servers_cfgs);
   inst = _instance_create(scfg);

   inst->session_id_timer = ecore_timer_add(1.0, _session_id_poller_cb, inst);
   inst->torrents_poller_timer = ecore_timer_add(1.0, _torrents_poller_cb, inst);
   _session_id_poller_cb(inst);

   Eo *win = elm_win_add(NULL, "Transmission", ELM_WIN_BASIC);

   Eo *o = elm_box_add(win);
   evas_object_size_hint_align_set(o, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(o, EVAS_HINT_EXPAND, 0.0);
   evas_object_show(o);
   elm_win_resize_object_add(win, o);
   efl_wref_add(o, &inst->main_box);

   evas_object_resize(win, 480, 480);
   evas_object_show(win);

   ecore_event_handler_add(ECORE_EXE_EVENT_DATA, _rsync_output_cb, inst);
   ecore_event_handler_add(ECORE_EXE_EVENT_DEL, _rsync_end_cb, inst);

   elm_run();

   _instance_delete(inst);
   elm_shutdown();
   ecore_con_shutdown();
   ecore_shutdown();
   eina_shutdown();
   return 0;
}
#endif
