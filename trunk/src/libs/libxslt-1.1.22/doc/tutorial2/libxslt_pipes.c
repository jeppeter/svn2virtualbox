/*
 * libxslt_pipes.c: a program for performing a series of XSLT
 * transformations
 *
 * Writen by Panos Louridas, based on libxslt_tutorial.c by John Fleck.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,  59 Temple Place - Suite 330, Cambridge, MA 02139, USA.
 *
 */ 

/*
 * Oracle GPL Disclaimer: For the avoidance of doubt, except that if any license choice
 * other than GPL or LGPL is available it will apply instead, Oracle elects to use only
 * the General Public License version 2 (GPLv2) at this time for any software where
 * a choice of GPL license versions is made available with the language indicating
 * that GPLv2 or any later version may be used, or where a choice of which version
 * of the GPL is applied is otherwise unspecified.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>

extern int xmlLoadExtDtdDefaultValue;

static void usage(const char *name) {
    printf("Usage: %s [options] stylesheet [stylesheet ...] file [file ...]\n",
            name);
    printf("      --out file: send output to file\n");
    printf("      --param name value: pass a (parameter,value) pair\n");
}

int main(int argc, char **argv) {
    int arg_indx;
    const char *params[16 + 1];
    int params_indx = 0;
    int stylesheet_indx = 0;
    int file_indx = 0;
    int i, j, k;
    FILE *output_file = stdout;
    xsltStylesheetPtr *stylesheets = 
        (xsltStylesheetPtr *) calloc(argc, sizeof(xsltStylesheetPtr));
    xmlDocPtr *files = (xmlDocPtr *) calloc(argc, sizeof(xmlDocPtr));
    xmlDocPtr doc, res;
    int return_value = 0;
        
    if (argc <= 1) {
        usage(argv[0]);
        return_value = 1;
        goto finish;
    }
        
    /* Collect arguments */
    for (arg_indx = 1; arg_indx < argc; arg_indx++) {
        if (argv[arg_indx][0] != '-')
            break;
        if ((!strcmp(argv[arg_indx], "-param"))
                || (!strcmp(argv[arg_indx], "--param"))) {
            arg_indx++;
            params[params_indx++] = argv[arg_indx++];
            params[params_indx++] = argv[arg_indx];
            if (params_indx >= 16) {
                fprintf(stderr, "too many params\n");
                return_value = 1;
                goto finish;
            }
        }  else if ((!strcmp(argv[arg_indx], "-o"))
                || (!strcmp(argv[arg_indx], "--out"))) {
            arg_indx++;
            output_file = fopen(argv[arg_indx], "w");
        } else {
            fprintf(stderr, "Unknown option %s\n", argv[arg_indx]);
            usage(argv[0]);
            return_value = 1;
            goto finish;
        }
    }
    params[params_indx] = 0;

    /* Collect and parse stylesheets and files to be transformed */
    for (; arg_indx < argc; arg_indx++) {
        char *argument =
            (char *) malloc(sizeof(char) * (strlen(argv[arg_indx]) + 1));
        strcpy(argument, argv[arg_indx]);
        if (strtok(argument, ".")) {
            char *suffix = strtok(0, ".");
            if (suffix && !strcmp(suffix, "xsl")) {
                stylesheets[stylesheet_indx++] =
                    xsltParseStylesheetFile((const xmlChar *)argv[arg_indx]);;
            } else {
                files[file_indx++] = xmlParseFile(argv[arg_indx]);
            }
        } else {
            files[file_indx++] = xmlParseFile(argv[arg_indx]);
        }
        free(argument);
    }

    xmlSubstituteEntitiesDefault(1);
    xmlLoadExtDtdDefaultValue = 1;

    /* Process files */
    for (i = 0; files[i]; i++) {
        doc = files[i];
        res = doc;
        for (j = 0; stylesheets[j]; j++) {
            res = xsltApplyStylesheet(stylesheets[j], doc, params);
            xmlFreeDoc(doc);
            doc = res;
        }

        if (stylesheets[0]) {
            xsltSaveResultToFile(output_file, res, stylesheets[j-1]);
        } else {
            xmlDocDump(output_file, res);
        }
        xmlFreeDoc(res);
    }

    fclose(output_file);

    for (k = 0; stylesheets[k]; k++) {
        xsltFreeStylesheet(stylesheets[k]);
    }

    xsltCleanupGlobals();
    xmlCleanupParser();

 finish:
    free(stylesheets);
    free(files);
    return(return_value);
}
