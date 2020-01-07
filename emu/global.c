/*
 *  Copyright (C) 2006 Ludovic Jacomme (ludovic.jacomme@gmail.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <fcntl.h>

#include "global.h"
#include "psp_ti99.h"
#include "psp_sdl.h"
#include "psp_kbd.h"
#include "psp_joy.h"
#include "psp_menu.h"
#include "psp_fmgr.h"

//LUDO:
TI99_t TI99;

u8 CpuMemory[0x10000];
u8 MemFlags[0x10000];

void update_save_name(char *Name)
{
  char TmpFileName[MAX_PATH];
  struct stat aStat;
  int index;
  char *SaveName;
  char *Scan1;
  char *Scan2;

  SaveName = strrchr(Name, '/');
  if (SaveName != (char *)0)
    SaveName++;
  else
    SaveName = Name;

  if (!strncasecmp(SaveName, "sav_", 4))
  {
    Scan1 = SaveName + 4;
    Scan2 = strrchr(Scan1, '_');
    if (Scan2 && (Scan2[1] >= '0') && (Scan2[1] <= '5'))
    {
      strncpy(TI99.ti99_save_name, Scan1, MAX_PATH);
      TI99.ti99_save_name[Scan2 - Scan1] = '\0';
      fprintf(stderr, "START: TI99.ti99_save_name: %s\n", TI99.ti99_save_name);
    }
    else
    {
      strncpy(TI99.ti99_save_name, SaveName, MAX_PATH);
      fprintf(stderr, "START: TI99.ti99_save_name: %s\n", TI99.ti99_save_name);
    }
  }
  else
  {
    strncpy(TI99.ti99_save_name, SaveName, MAX_PATH);
    fprintf(stderr, "START: TI99.ti99_save_name: %s\n", TI99.ti99_save_name);    
  }

  if (TI99.ti99_save_name[0] == '\0')
  {
    strcpy(TI99.ti99_save_name, "default");
    fprintf(stderr, "START: TI99.ti99_save_name: %s\n", TI99.ti99_save_name);
  }

  for (index = 0; index < TI99_MAX_SAVE_STATE; index++)
  {
    TI99.ti99_save_state[index].used = 0;
    memset(&TI99.ti99_save_state[index].date, 0, sizeof(time_t));
    TI99.ti99_save_state[index].thumb = 0;

    snprintf(TmpFileName, MAX_PATH, "%s/save/sav_%s_%d.img", TI99.ti99_home_dir, TI99.ti99_save_name, index);
    if (!stat(TmpFileName, &aStat))
    {
      TI99.ti99_save_state[index].used = 1;
      TI99.ti99_save_state[index].date = aStat.st_mtime;
      snprintf(TmpFileName, MAX_PATH, "%s/save/sav_%s_%d.png", TI99.ti99_home_dir, TI99.ti99_save_name, index);
      if (!stat(TmpFileName, &aStat))
      {
        if (psp_sdl_load_thumb_png(TI99.ti99_save_state[index].surface, TmpFileName))
        {
          TI99.ti99_save_state[index].thumb = 1;
        }
      }
    }
  }

  TI99.comment_present = 0;
  snprintf(TmpFileName, MAX_PATH, "%s/txt/%s.txt", TI99.ti99_home_dir, TI99.ti99_save_name);
  if (!stat(TmpFileName, &aStat))
  {
    TI99.comment_present = 1;
  }
}

void reset_save_name()
{
  update_save_name("");
}

typedef struct thumb_list
{
  struct thumb_list *next;
  char *name;
  char *thumb;
} thumb_list;

static thumb_list *loc_head_thumb = 0;

static void
loc_del_thumb_list()
{
  while (loc_head_thumb != 0)
  {
    thumb_list *del_elem = loc_head_thumb;
    loc_head_thumb = loc_head_thumb->next;
    if (del_elem->name)
      free(del_elem->name);
    if (del_elem->thumb)
      free(del_elem->thumb);
    free(del_elem);
  }
}

static void
loc_add_thumb_list(char *filename)
{
  thumb_list *new_elem;
  char tmp_filename[MAX_PATH];

  strcpy(tmp_filename, filename);
  char *save_name = tmp_filename;

  /* .png extention */
  char *Scan = strrchr(save_name, '.');
  if ((!Scan) || (strcasecmp(Scan, ".png")))
    return;
  *Scan = 0;

  if (strncasecmp(save_name, "sav_", 4))
    return;
  save_name += 4;

  Scan = strrchr(save_name, '_');
  if (!Scan)
    return;
  *Scan = 0;

  /* only one png for a given save name */
  new_elem = loc_head_thumb;
  while (new_elem != 0)
  {
    if (!strcasecmp(new_elem->name, save_name))
      return;
    new_elem = new_elem->next;
  }

  new_elem = (thumb_list *)malloc(sizeof(thumb_list));
  new_elem->next = loc_head_thumb;
  loc_head_thumb = new_elem;
  new_elem->name = strdup(save_name);
  new_elem->thumb = strdup(filename);
}

void load_thumb_list()
{
  char SaveDirName[MAX_PATH];
  DIR *fd = 0;

  loc_del_thumb_list();

  snprintf(SaveDirName, MAX_PATH, "%s/save", TI99.ti99_home_dir);

  fd = opendir(SaveDirName);
  if (!fd)
    return;

  struct dirent *a_dirent;
  while ((a_dirent = readdir(fd)) != 0)
  {
    if (a_dirent->d_name[0] == '.')
      continue;
    if (a_dirent->d_type != DT_DIR)
    {
      loc_add_thumb_list(a_dirent->d_name);
    }
  }
  closedir(fd);
}

int load_thumb_if_exists(char *Name)
{
  char FileName[MAX_PATH];
  char ThumbFileName[MAX_PATH];
  struct stat aStat;
  char *SaveName;
  char *Scan;

  strcpy(FileName, Name);
  SaveName = strrchr(FileName, '/');
  if (SaveName != (char *)0)
    SaveName++;
  else
    SaveName = FileName;

  Scan = strrchr(SaveName, '.');
  if (Scan)
    *Scan = '\0';

  if (!SaveName[0])
    return 0;

  thumb_list *scan_list = loc_head_thumb;
  while (scan_list != 0)
  {
    if (!strcasecmp(SaveName, scan_list->name))
    {
      snprintf(ThumbFileName, MAX_PATH, "%s/save/%s", TI99.ti99_home_dir, scan_list->thumb);
      if (!stat(ThumbFileName, &aStat))
      {
        if (psp_sdl_load_thumb_png(save_surface, ThumbFileName))
        {
          return 1;
        }
      }
    }
    scan_list = scan_list->next;
  }
  return 0;
}

typedef struct comment_list
{
  struct comment_list *next;
  char *name;
  char *filename;
} comment_list;

static comment_list *loc_head_comment = 0;

static void
loc_del_comment_list()
{
  while (loc_head_comment != 0)
  {
    comment_list *del_elem = loc_head_comment;
    loc_head_comment = loc_head_comment->next;
    if (del_elem->name)
      free(del_elem->name);
    if (del_elem->filename)
      free(del_elem->filename);
    free(del_elem);
  }
}

static void
loc_add_comment_list(char *filename)
{
  comment_list *new_elem;
  char tmp_filename[MAX_PATH];

  strcpy(tmp_filename, filename);
  char *save_name = tmp_filename;

  /* .png extention */
  char *Scan = strrchr(save_name, '.');
  if ((!Scan) || (strcasecmp(Scan, ".txt")))
    return;
  *Scan = 0;

  /* only one txt for a given save name */
  new_elem = loc_head_comment;
  while (new_elem != 0)
  {
    if (!strcasecmp(new_elem->name, save_name))
      return;
    new_elem = new_elem->next;
  }

  new_elem = (comment_list *)malloc(sizeof(comment_list));
  new_elem->next = loc_head_comment;
  loc_head_comment = new_elem;
  new_elem->name = strdup(save_name);
  new_elem->filename = strdup(filename);
}

void load_comment_list()
{
  char SaveDirName[MAX_PATH];
  DIR *fd = 0;

  loc_del_comment_list();

  snprintf(SaveDirName, MAX_PATH, "%s/txt", TI99.ti99_home_dir);

  fd = opendir(SaveDirName);
  if (!fd)
    return;

  struct dirent *a_dirent;
  while ((a_dirent = readdir(fd)) != 0)
  {
    if (a_dirent->d_name[0] == '.')
      continue;
    if (a_dirent->d_type != DT_DIR)
    {
      loc_add_comment_list(a_dirent->d_name);
    }
  }
  closedir(fd);
}

char *
load_comment_if_exists(char *Name)
{
  static char loc_comment_buffer[128];

  char FileName[MAX_PATH];
  char TmpFileName[MAX_PATH];
  FILE *a_file;
  char *SaveName;
  char *Scan;

  loc_comment_buffer[0] = 0;

  strcpy(FileName, Name);
  SaveName = strrchr(FileName, '/');
  if (SaveName != (char *)0)
    SaveName++;
  else
    SaveName = FileName;

  Scan = strrchr(SaveName, '.');
  if (Scan)
    *Scan = '\0';

  if (!SaveName[0])
    return 0;

  comment_list *scan_list = loc_head_comment;
  while (scan_list != 0)
  {
    if (!strcasecmp(SaveName, scan_list->name))
    {
      snprintf(TmpFileName, MAX_PATH, "%s/txt/%s", TI99.ti99_home_dir, scan_list->filename);
      a_file = fopen(TmpFileName, "r");
      if (a_file)
      {
        char *a_scan = 0;
        loc_comment_buffer[0] = 0;
        if (fgets(loc_comment_buffer, 60, a_file) != 0)
        {
          a_scan = strchr(loc_comment_buffer, '\n');
          if (a_scan)
            *a_scan = '\0';
          /* For this #@$% of windows ! */
          a_scan = strchr(loc_comment_buffer, '\r');
          if (a_scan)
            *a_scan = '\0';
          fclose(a_file);
          return loc_comment_buffer;
        }
        fclose(a_file);
        return 0;
      }
    }
    scan_list = scan_list->next;
  }
  return 0;
}

void myPowerSetClockFrequency(int cpu_clock)
{
  if (TI99.ti99_current_clock != cpu_clock)
  {
    gp2xPowerSetClockFrequency(cpu_clock);
    TI99.ti99_current_clock = cpu_clock;
  }
}

void ti99_default_settings()
{
  TI99.ti99_snd_enable = 1;
  TI99.ti99_render_mode = TI99_RENDER_FIT;
  TI99.ti99_speed_limiter = 60;
  TI99.psp_screenshot_id = 0;
  TI99.psp_cpu_clock = GP2X_DEF_EMU_CLOCK;
  TI99.ti99_vsync = 0;
  TI99.danzeff_trans = 1;
  TI99.ti99_view_fps = 1;

  myPowerSetClockFrequency(TI99.psp_cpu_clock);
}

int loc_ti99_save_settings(char *chFileName)
{
  FILE *FileDesc;
  int error = 0;

  FileDesc = fopen(chFileName, "w");
  if (FileDesc != (FILE *)0)
  {

    fprintf(FileDesc, "psp_cpu_clock=%d\n", TI99.psp_cpu_clock);
    fprintf(FileDesc, "danzeff_trans=%d\n", TI99.danzeff_trans);
    fprintf(FileDesc, "psp_skip_max_frame=%d\n", TI99.psp_skip_max_frame);
    fprintf(FileDesc, "ti99_snd_enable=%d\n", TI99.ti99_snd_enable);
    fprintf(FileDesc, "ti99_render_mode=%d\n", TI99.ti99_render_mode);
    fprintf(FileDesc, "ti99_speed_limiter=%d\n", TI99.ti99_speed_limiter);
    fprintf(FileDesc, "ti99_view_fps=%d\n", TI99.ti99_view_fps);
    fprintf(FileDesc, "ti99_vsync=%d\n", TI99.ti99_vsync);

    fclose(FileDesc);

    fprintf(stderr, "SAVE CONFIG: TI99.psp_cpu_clock: %u\n", TI99.psp_cpu_clock);
    fprintf(stderr, "SAVE CONFIG: TI99.danzeff_trans: %u\n", TI99.danzeff_trans);
    fprintf(stderr, "SAVE CONFIG: TI99.psp_skip_max_frame: %u\n", TI99.psp_skip_max_frame);
    fprintf(stderr, "SAVE CONFIG: TI99.ti99_snd_enable: %u\n", TI99.ti99_snd_enable);
    fprintf(stderr, "SAVE CONFIG: TI99.ti99_render_mode: %u\n", TI99.ti99_render_mode);
    fprintf(stderr, "SAVE CONFIG: TI99.ti99_speed_limiter: %u\n", TI99.ti99_speed_limiter);
    fprintf(stderr, "SAVE CONFIG: TI99.ti99_view_fps: %u\n", TI99.ti99_view_fps);
    fprintf(stderr, "SAVE CONFIG: TI99.ti99_vsync: %u\n", TI99.ti99_vsync);
  }
  else
  {
    error = 1;
  }

  return error;
}

int ti99_save_settings(void)
{
  char FileName[MAX_PATH + 1];
  int error;

  error = 1;

  snprintf(FileName, MAX_PATH, "%s/set/%s.set", TI99.ti99_home_dir, TI99.ti99_save_name);
  error = loc_ti99_save_settings(FileName);

  return error;
}

static int
loc_ti99_load_settings(char *chFileName)
{
  char Buffer[512];
  char *Scan;
  unsigned int Value;
  FILE *FileDesc;

  FileDesc = fopen(chFileName, "r");
  if (FileDesc == (FILE *)0)
    return 0;

  while (fgets(Buffer, 512, FileDesc) != (char *)0)
  {

    Scan = strchr(Buffer, '\n');
    if (Scan)
      *Scan = '\0';

    /* For this #@$% of windows ! */
    Scan = strchr(Buffer, '\r');
    if (Scan)
      *Scan = '\0';
    if (Buffer[0] == '#')
      continue;

    Scan = strchr(Buffer, '=');
    if (!Scan)
      continue;

    *Scan = '\0';
    Value = atoi(Scan + 1);

    if (!strcasecmp(Buffer, "psp_cpu_clock"))
      TI99.psp_cpu_clock = Value;
    else if (!strcasecmp(Buffer, "danzeff_trans"))
      TI99.danzeff_trans = Value;
    else if (!strcasecmp(Buffer, "psp_skip_max_frame"))
      TI99.psp_skip_max_frame = Value;
    else if (!strcasecmp(Buffer, "ti99_snd_enable"))
      TI99.ti99_snd_enable = Value;
    else if (!strcasecmp(Buffer, "ti99_render_mode"))
      TI99.ti99_render_mode = Value;
    else if (!strcasecmp(Buffer, "ti99_speed_limiter"))
      TI99.ti99_speed_limiter = Value;
    else if (!strcasecmp(Buffer, "ti99_view_fps"))
      TI99.ti99_view_fps = Value;
    else if (!strcasecmp(Buffer, "ti99_vsync"))
      TI99.ti99_vsync = Value;
  }

  fprintf(stderr, "LOAD CONFIG: TI99.psp_cpu_clock: %u\n", TI99.psp_cpu_clock);
  fprintf(stderr, "LOAD CONFIG: TI99.danzeff_trans: %u\n", TI99.danzeff_trans);
  fprintf(stderr, "LOAD CONFIG: TI99.psp_skip_max_frame: %u\n", TI99.psp_skip_max_frame);
  fprintf(stderr, "LOAD CONFIG: TI99.ti99_snd_enable: %u\n", TI99.ti99_snd_enable);
  fprintf(stderr, "LOAD CONFIG: TI99.ti99_render_mode: %u\n", TI99.ti99_render_mode);
  fprintf(stderr, "LOAD CONFIG: TI99.ti99_speed_limiter: %u\n", TI99.ti99_speed_limiter);
  fprintf(stderr, "LOAD CONFIG: TI99.ti99_view_fps: %u\n", TI99.ti99_view_fps);
  fprintf(stderr, "LOAD CONFIG: TI99.ti99_vsync: %u\n", TI99.ti99_vsync);

  fclose(FileDesc);

  myPowerSetClockFrequency(TI99.psp_cpu_clock);

  return 0;
}

int ti99_load_settings()
{
  char FileName[MAX_PATH + 1];
  int error;

  error = 1;

  snprintf(FileName, MAX_PATH, "%s/set/%s.set", TI99.ti99_home_dir, TI99.ti99_save_name);
  fprintf(stderr, "LOAD CONFIG: TI99.ti99_save_name: %s\n", TI99.ti99_save_name);
  fprintf(stderr, "LOAD CONFIG: TI99.ti99_home_dir: %s\n", TI99.ti99_home_dir);
  error = loc_ti99_load_settings(FileName);
  fprintf(stderr, "LOAD CONFIG: error: %u\n", error);

  return error;
}

int ti99_load_file_settings(char *FileName)
{
  return loc_ti99_load_settings(FileName);
}

static int
loc_load_ctg(char *TmpName)
{
  int error;
  error = ti99_load_cartridge(TmpName);
  return error;
}

void ti99_kbd_load(void)
{
  char TmpFileName[MAX_PATH + 1];
  struct stat aStat;

  snprintf(TmpFileName, MAX_PATH, "%s/kbd/%s.kbd", TI99.ti99_home_dir, TI99.ti99_save_name);
  if (!stat(TmpFileName, &aStat))
  {
    psp_kbd_load_mapping(TmpFileName);
  }
}

int ti99_kbd_save(void)
{
  char TmpFileName[MAX_PATH + 1];
  snprintf(TmpFileName, MAX_PATH, "%s/kbd/%s.kbd", TI99.ti99_home_dir, TI99.ti99_save_name);
  return (psp_kbd_save_mapping(TmpFileName));
}

void ti99_joy_load(void)
{
  char TmpFileName[MAX_PATH + 1];
  struct stat aStat;

  snprintf(TmpFileName, MAX_PATH, "%s/joy/%s.joy", TI99.ti99_home_dir, TI99.ti99_save_name);
  if (!stat(TmpFileName, &aStat))
  {
    psp_joy_load_settings(TmpFileName);
  }
}

int ti99_joy_save(void)
{
  char TmpFileName[MAX_PATH + 1];
  snprintf(TmpFileName, MAX_PATH, "%s/joy/%s.joy", TI99.ti99_home_dir, TI99.ti99_save_name);
  return (psp_joy_save_settings(TmpFileName));
}

//Load Functions

typedef unsigned int dword;
typedef unsigned short word;
typedef unsigned char byte;

#define ERR_FILE_NOT_FOUND 13
#define ERR_FILE_BAD_ZIP 14
#define ERR_FILE_EMPTY_ZIP 15
#define ERR_FILE_UNZIP_FAILED 16

int ti99_load_ctg(char *FileName)
{
  char *scan;
  char SaveName[MAX_PATH + 1];
  int error;

  error = 1;

  strncpy(SaveName, FileName, MAX_PATH);
  scan = strrchr(SaveName, '.');
  if (scan)
    *scan = '\0';
  update_save_name(SaveName);
  error = loc_load_ctg(FileName);

  if (!error)
  {
    ti99_kbd_load();
    ti99_joy_load();
    ti99_load_cheat();
    ti99_load_settings();
  }

  return error;
}

static int
loc_load_state(char *filename)
{
  int error;
  error = !ti99_load_img(filename);
  return error;
}

int ti99_load_state(char *FileName)
{
  char *scan;
  char SaveName[MAX_PATH + 1];
  int error;

  error = 1;

  strncpy(SaveName, FileName, MAX_PATH);
  scan = strrchr(SaveName, '.');
  if (scan)
    *scan = '\0';
  update_save_name(SaveName);
  error = ti99_load_img(FileName);

  if (!error)
  {
    ti99_kbd_load();
    ti99_joy_load();
    ti99_load_cheat();
    ti99_load_settings();
  }

  return error;
}

int ti99_snapshot_save_slot(int save_id)
{
  char FileName[MAX_PATH + 1];
  struct stat aStat;
  int error;

  error = 1;

  if (save_id < TI99_MAX_SAVE_STATE)
  {
    snprintf(FileName, MAX_PATH, "%s/save/sav_%s_%d.img", TI99.ti99_home_dir, TI99.ti99_save_name, save_id);
    error = ti99_save_img(FileName);
    if (!error)
    {
      if (!stat(FileName, &aStat))
      {
        TI99.ti99_save_state[save_id].used = 1;
        TI99.ti99_save_state[save_id].thumb = 0;
        TI99.ti99_save_state[save_id].date = aStat.st_mtime;
        snprintf(FileName, MAX_PATH, "%s/save/sav_%s_%d.png", TI99.ti99_home_dir, TI99.ti99_save_name, save_id);
        if (psp_sdl_save_thumb_png(TI99.ti99_save_state[save_id].surface, FileName))
        {
          TI99.ti99_save_state[save_id].thumb = 1;
        }
      }
    }
  }

  return error;
}

int ti99_snapshot_load_slot(int load_id)
{
  char FileName[MAX_PATH + 1];
  int error;

  error = 1;

  if (load_id < TI99_MAX_SAVE_STATE)
  {
    snprintf(FileName, MAX_PATH, "%s/save/sav_%s_%d.img", TI99.ti99_home_dir, TI99.ti99_save_name, load_id);
    error = loc_load_state(FileName);
  }
  return error;
}

int ti99_snapshot_del_slot(int save_id)
{
  char FileName[MAX_PATH + 1];
  struct stat aStat;
  int error;

  error = 1;

  if (save_id < TI99_MAX_SAVE_STATE)
  {
    snprintf(FileName, MAX_PATH, "%s/save/sav_%s_%d.img", TI99.ti99_home_dir, TI99.ti99_save_name, save_id);
    error = remove(FileName);
    if (!error)
    {
      TI99.ti99_save_state[save_id].used = 0;
      TI99.ti99_save_state[save_id].thumb = 0;
      memset(&TI99.ti99_save_state[save_id].date, 0, sizeof(time_t));

      /* We keep always thumbnail with id 0, to have something to display in the file requester */
      if (save_id != 0)
      {
        snprintf(FileName, MAX_PATH, "%s/save/sav_%s_%d.png", TI99.ti99_home_dir, TI99.ti99_save_name, save_id);
        if (!stat(FileName, &aStat))
        {
          remove(FileName);
        }
      }
    }
  }

  return error;
}

static int
loc_ti99_save_cheat(char *chFileName)
{
  FILE *FileDesc;
  int cheat_num;
  int error = 0;

  FileDesc = fopen(chFileName, "w");
  if (FileDesc != (FILE *)0)
  {

    for (cheat_num = 0; cheat_num < TI99_MAX_CHEAT; cheat_num++)
    {
      TI99_cheat_t *a_cheat = &TI99.ti99_cheat[cheat_num];
      if (a_cheat->type != TI99_CHEAT_NONE)
      {
        fprintf(FileDesc, "%d,%x,%x,%s\n",
                a_cheat->type, a_cheat->addr, a_cheat->value, a_cheat->comment);
      }
    }
    fclose(FileDesc);
  }
  else
  {
    error = 1;
  }

  return error;
}

int ti99_save_cheat(void)
{
  char FileName[MAX_PATH + 1];
  int error;

  error = 1;

  snprintf(FileName, MAX_PATH, "%s/cht/%s.cht", TI99.ti99_home_dir, TI99.ti99_save_name);
  error = loc_ti99_save_cheat(FileName);

  return error;
}

static int
loc_ti99_load_cheat(char *chFileName)
{
  char Buffer[512];
  char *Scan;
  char *Field;
  unsigned int cheat_addr;
  unsigned int cheat_value;
  unsigned int cheat_type;
  char *cheat_comment;
  int cheat_num;
  FILE *FileDesc;

  memset(TI99.ti99_cheat, 0, sizeof(TI99.ti99_cheat));
  cheat_num = 0;

  FileDesc = fopen(chFileName, "r");
  if (FileDesc == (FILE *)0)
    return 0;

  while (fgets(Buffer, 512, FileDesc) != (char *)0)
  {

    Scan = strchr(Buffer, '\n');
    if (Scan)
      *Scan = '\0';
    /* For this #@$% of windows ! */
    Scan = strchr(Buffer, '\r');
    if (Scan)
      *Scan = '\0';
    if (Buffer[0] == '#')
      continue;

    /* %d, %x, %x, %s */
    Field = Buffer;
    Scan = strchr(Field, ',');
    if (!Scan)
      continue;
    *Scan = 0;
    if (sscanf(Field, "%d", &cheat_type) != 1)
      continue;
    Field = Scan + 1;
    Scan = strchr(Field, ',');
    if (!Scan)
      continue;
    *Scan = 0;
    if (sscanf(Field, "%x", &cheat_addr) != 1)
      continue;
    Field = Scan + 1;
    Scan = strchr(Field, ',');
    if (!Scan)
      continue;
    *Scan = 0;
    if (sscanf(Field, "%x", &cheat_value) != 1)
      continue;
    Field = Scan + 1;
    cheat_comment = Field;

    if (cheat_type <= TI99_CHEAT_NONE)
      continue;

    TI99_cheat_t *a_cheat = &TI99.ti99_cheat[cheat_num];

    a_cheat->type = cheat_type;
    a_cheat->addr = cheat_addr % TI99_RAM_SIZE;
    a_cheat->value = cheat_value;
    strncpy(a_cheat->comment, cheat_comment, sizeof(a_cheat->comment));
    a_cheat->comment[sizeof(a_cheat->comment) - 1] = 0;

    if (++cheat_num >= TI99_MAX_CHEAT)
      break;
  }
  fclose(FileDesc);

  return 0;
}

int ti99_load_cheat()
{
  char FileName[MAX_PATH + 1];
  int error;

  error = 1;

  snprintf(FileName, MAX_PATH, "%s/cht/%s.cht", TI99.ti99_home_dir, TI99.ti99_save_name);
  error = loc_ti99_load_cheat(FileName);

  return error;
}

int ti99_load_file_cheat(char *FileName)
{
  return loc_ti99_load_cheat(FileName);
}

void ti99_apply_cheats()
{
  int cheat_num;
  for (cheat_num = 0; cheat_num < TI99_MAX_CHEAT; cheat_num++)
  {
    TI99_cheat_t *a_cheat = &TI99.ti99_cheat[cheat_num];
    if (a_cheat->type == TI99_CHEAT_ENABLE)
    {
      CpuMemory[a_cheat->addr] = a_cheat->value;
    }
  }
}

void ti99_audio_pause()
{
  if (TI99.ti99_snd_enable)
  {
    SDL_PauseAudio(1);
  }
}

void ti99_audio_resume()
{
  if (TI99.ti99_snd_enable)
  {
    SDL_PauseAudio(0);
  }
}

void ti99_global_init()
{
  memset(&TI99, 0, sizeof(TI99_t));
  getcwd(TI99.ti99_home_dir, MAX_PATH);

  // Get init file directory & name
  strcpy(TI99.ti99_install_dir, ".");
  fprintf(stderr, "START: ti99_install_dir: %s\n", TI99.ti99_install_dir);

  // Get init file directory & name
  snprintf(TI99.ti99_home_dir, sizeof(TI99.ti99_home_dir), "%s/.ti99", getenv("HOME"));
  mkdir(TI99.ti99_home_dir, 0777);
  fprintf(stderr, "START: ti99_home_dir: %s\n", TI99.ti99_home_dir);

  char ti99_tempdir[512];
  snprintf(ti99_tempdir, sizeof(ti99_tempdir), "%s/scr", TI99.ti99_home_dir);
  mkdir(ti99_tempdir, 0777);
  fprintf(stderr, "START: ti99_scr_dir: %s\n", ti99_tempdir);

  snprintf(ti99_tempdir, sizeof(ti99_tempdir), "%s/set", TI99.ti99_home_dir);
  mkdir(ti99_tempdir, 0777);
  fprintf(stderr, "START: ti99_set_dir: %s\n", ti99_tempdir);

  snprintf(ti99_tempdir, sizeof(ti99_tempdir), "%s/txt", TI99.ti99_home_dir);
  mkdir(ti99_tempdir, 0777);
  fprintf(stderr, "START: ti99_txt_dir: %s\n", ti99_tempdir);

  snprintf(ti99_tempdir, sizeof(ti99_tempdir), "%s/save", TI99.ti99_home_dir);
  mkdir(ti99_tempdir, 0777);
  fprintf(stderr, "START: ti99_save_dir: %s\n", ti99_tempdir);

  snprintf(ti99_tempdir, sizeof(ti99_tempdir), "%s/kbd", TI99.ti99_home_dir);
  mkdir(ti99_tempdir, 0777);
  fprintf(stderr, "START: ti99_kbd_dir: %s\n", ti99_tempdir);

  snprintf(ti99_tempdir, sizeof(ti99_tempdir), "%s/joy", TI99.ti99_home_dir);
  mkdir(ti99_tempdir, 0777);
  fprintf(stderr, "START: ti99_joy_dir: %s\n", ti99_tempdir);

  snprintf(ti99_tempdir, sizeof(ti99_tempdir), "%s/cht", TI99.ti99_home_dir);
  mkdir(ti99_tempdir, 0777);
  fprintf(stderr, "START: ti99_cht_dir: %s\n", ti99_tempdir);

  snprintf(ti99_tempdir, sizeof(ti99_tempdir), "%s/roms", TI99.ti99_home_dir);
  mkdir(ti99_tempdir, 0777);
  fprintf(stderr, "START: ti99_cht_dir: %s\n", ti99_tempdir);

  char ti99_tempfile_source[512];
  char ti99_tempfile_dest[512];
  snprintf(ti99_tempfile_source, sizeof(ti99_tempfile_source), "./roms/%s", "spchrom.bin");
  snprintf(ti99_tempfile_dest, sizeof(ti99_tempfile_dest), "%s/roms/%s", TI99.ti99_home_dir, "spchrom.bin");
  fprintf(stderr, "START: ti99_copy_file source: %s\n", ti99_tempfile_source);
  fprintf(stderr, "START: ti99_copy_file dest: %s\n", ti99_tempfile_dest);
  ti99_copy_file(ti99_tempfile_source, ti99_tempfile_dest);

  snprintf(ti99_tempfile_source, sizeof(ti99_tempfile_source), "./roms/%s", "spchrom.dat");
  snprintf(ti99_tempfile_dest, sizeof(ti99_tempfile_dest), "%s/roms/%s", TI99.ti99_home_dir, "spchrom.dat");
  fprintf(stderr, "START: ti99_copy_file source: %s\n", ti99_tempfile_source);
  fprintf(stderr, "START: ti99_copy_file dest: %s\n", ti99_tempfile_dest);
  ti99_copy_file(ti99_tempfile_source, ti99_tempfile_dest);

  snprintf(ti99_tempfile_source, sizeof(ti99_tempfile_source), "./roms/%s", "ti-994a.ctg");
  snprintf(ti99_tempfile_dest, sizeof(ti99_tempfile_dest), "%s/roms/%s", TI99.ti99_home_dir, "ti-994a.ctg");
  fprintf(stderr, "START: ti99_copy_file source: %s\n", ti99_tempfile_source);
  fprintf(stderr, "START: ti99_copy_file dest: %s\n", ti99_tempfile_dest);
  ti99_copy_file(ti99_tempfile_source, ti99_tempfile_dest);

  snprintf(ti99_tempfile_source, sizeof(ti99_tempfile_source), "./roms/%s", "ti-994a.dat");
  snprintf(ti99_tempfile_dest, sizeof(ti99_tempfile_dest), "%s/roms/%s", TI99.ti99_home_dir, "ti-994a.dat");
  fprintf(stderr, "START: ti99_copy_file source: %s\n", ti99_tempfile_source);
  fprintf(stderr, "START: ti99_copy_file dest: %s\n", ti99_tempfile_dest);
  ti99_copy_file(ti99_tempfile_source, ti99_tempfile_dest);

  snprintf(ti99_tempfile_source, sizeof(ti99_tempfile_source), "./set/%s", "default.set");
  snprintf(ti99_tempfile_dest, sizeof(ti99_tempfile_dest), "%s/set/%s", TI99.ti99_home_dir, "default.set");
  fprintf(stderr, "START: ti99_copy_file source: %s\n", ti99_tempfile_source);
  fprintf(stderr, "START: ti99_copy_file dest: %s\n", ti99_tempfile_dest);
  ti99_copy_file(ti99_tempfile_source, ti99_tempfile_dest);

  psp_sdl_init();

  ti99_default_settings();
  psp_joy_default_settings();
  psp_kbd_default_settings();

  update_save_name("");
  ti99_kbd_load();
  ti99_joy_load();
  ti99_load_cheat();
  ti99_load_settings();

  myPowerSetClockFrequency(TI99.psp_cpu_clock);
}

int ti99_file_exists(const char *filename)
{
  struct stat buffer;
  int exist = stat(filename, &buffer);
  if (exist == 0)
    return 1;
  else // -1
    return 0;
}

int ti99_copy_file(const char *source, const char *destination)
{
  if (ti99_file_exists(destination))
  {
    fprintf(stderr, "File %s exist.\n", destination);
    return 0;
  }
  else
  {
    fprintf(stderr, "File %s does not exist.\n", destination);
    int input, output;
    if ((input = open(source, O_RDONLY)) == -1)
    {
      return -1;
    }
    if ((output = creat(destination, 0660)) == -1)
    {
      close(input);
      return -1;
    }

    //sendfile will work with non-socket output (i.e. regular file) on Linux 2.6.33+
    off_t bytesCopied = 0;
    struct stat fileinfo = {0};
    fstat(input, &fileinfo);
    int result = sendfile(output, input, &bytesCopied, fileinfo.st_size);

    close(input);
    close(output);

    return result;
  }
}

void ti99_treat_command_key(int ti99_idx)
{
  int new_render;

  switch (ti99_idx)
  {
  case TI99_C_FPS:
    TI99.ti99_view_fps = !TI99.ti99_view_fps;
    break;
  case TI99_C_JOY:
    TI99.psp_reverse_analog = !TI99.psp_reverse_analog;
    break;
  case TI99_C_RENDER:
    psp_sdl_black_screen();
    new_render = TI99.ti99_render_mode + 1;
    if (new_render > TI99_LAST_RENDER)
      new_render = 0;
    TI99.ti99_render_mode = new_render;
    break;
  case TI99_C_LOAD:
    psp_main_menu_load_current();
    break;
  case TI99_C_SAVE:
    psp_main_menu_save_current();
    break;
  case TI99_C_RESET:
    psp_sdl_black_screen();
    ti99_reset_computer();
    reset_save_name();
    break;
  case TI99_C_AUTOFIRE:
    kbd_change_auto_fire(!TI99.ti99_auto_fire);
    break;
  case TI99_C_DECFIRE:
    if (TI99.ti99_auto_fire_period > 0)
      TI99.ti99_auto_fire_period--;
    break;
  case TI99_C_INCFIRE:
    if (TI99.ti99_auto_fire_period < 19)
      TI99.ti99_auto_fire_period++;
    break;
  case TI99_C_SCREEN:
    psp_screenshot_mode = 10;
    break;
  }
}
