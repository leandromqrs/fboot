/*
 * config.c - global settings and config file parser
 *
 * Copyright (C) 2020 by Leandro Rodrigues
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

#include "config.h"
#include "ini.h"

#include <fcntl.h>
#include <string.h>
#include <stdio.h>
//#include <global.h>
#include <stdlib.h>

CONFIGURATION config;

static int handler(void* user, const char* section, const char* name,
                   const char* value)
{
    CONFIGURATION* pconfig = (CONFIGURATION*)user;
    printf("EPA1 %p\n", &pconfig);
    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("Global", "LogLevel")) {
        pconfig->loglevel = atoi(value);
    } else if (MATCH("Global", "Verbose")) {
        pconfig->verbose = atoi(value); 
    } else if (MATCH("Global", "Init")) {
        pconfig->init = strdup(value);
    } else if (MATCH("Global", "LogTarget")) {
        pconfig->log_target = strdup(value);
    } else if (MATCH("Global", "StartupLogFile")) {
        pconfig->startup_log_file = strdup(value);
    } else if (MATCH("Collect", "EXT4Only")) {
        pconfig->ext4_only = atoi(value);
    } else {
        return 0;  /* unknown section/name, error */
    }
    return 1;
}

void *getConfig(char *name)
{

    if (!strcmp(name, "LogTarget")) {
        char *value = malloc(sizeof(char));
        value = config.log_target;
        return value;
    } else if (!strcmp(name, "LogLevel")) {
        int *value = malloc(sizeof(int));
        *value = config.loglevel;
        return value;
    } else if (!strcmp(name, "Verbose")) {
        int *value = malloc(sizeof(int));
        *value = config.verbose;
        return value;
    } else if (!strcmp(name, "StartupLogFile")) {
        char *value = malloc(sizeof(char));
        value = config.startup_log_file;
        return value;
    }
    return NULL;
}

int loadConfig()
{
    if (ini_parse("/etc/fboot.conf", handler, &config) < 0) {
        return 1;
    }
    return 0;
}
