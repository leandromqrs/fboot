/*
 * config.h - global settings and config file parser
 *
 * Copyright (C) 2020 by Leandro M. Rodrigues
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

typedef struct
{
    int loglevel;
    int verbose;
    bool ext4_only;
    char* defrag_mode;
    bool exclude_open_files;
    int timeout;
    char* log_target;
    char* init;
    bool force;
    char* startup_log_file;
} CONFIGURATION;

int loadConfig();

void* getConfig(char *name);

void setConfig(char *name, void *value);

#endif
