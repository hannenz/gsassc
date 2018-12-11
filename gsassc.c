/*
gsassc

Sass CSS Preprocesser

This is a GLib based port of [libsass](git@github.com/hcatlin/libsass)

Copyright 2013 Johannes Braun <me@hannenz.de>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <gio/gio.h>

/* #include <sass_context.h> */
#include <sass.h>

#define GSASSC_VERSION "0.2"

/* Default options */
static gchar *outfile = NULL;
static gchar *style = "nested";
static gboolean line_numbers = FALSE;
static gboolean source_map = FALSE;
static gchar *include_paths = NULL;
static gboolean verbose = FALSE;
static gboolean show_version = FALSE;
static gboolean show_libsass_version = FALSE;
static int precision = 4;

static GOptionEntry entries [] = {
	{	"output", 'o', 0, G_OPTION_ARG_FILENAME, &outfile, "Write to specified file", NULL},
	{	"style", 't', 0, G_OPTION_ARG_STRING, &style, "Output style. Can be nested, expanded, compact or compressed (Note: nested, compact and expanded are the same by now)" , NULL},
	{	"line-numbers", 'l', 0, G_OPTION_ARG_NONE, &line_numbers, "Emit comments showing original line numbers.", NULL},
	{	"source-map", 'g', 0, G_OPTION_ARG_NONE, &source_map, "Emit source map.", NULL},
	{	"import-path", 'I', 0, G_OPTION_ARG_STRING, &include_paths, "Set Sass import path (colon delimited list of paths).", NULL},
	{	"precision", 'p', 0, G_OPTION_ARG_INT, &precision, "Precision for outputing decimal values", NULL },
	{	"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Be verbose", NULL},
	{	"version", 'V', 0, G_OPTION_ARG_NONE, &show_version, "Show version", NULL },
	{	"libsass-version", 'L', 0, G_OPTION_ARG_NONE, &show_libsass_version, "Show version of libsass library", NULL },
	{ NULL }
};

static gchar *read_from_stdin () {

	GIOChannel *ch;
	GString *file, *line;
	GError *error = NULL;
	GIOStatus status;

	file = g_string_new (NULL);
	line = g_string_new (NULL);
	int fd = STDIN_FILENO;

	ch = g_io_channel_unix_new (fd);

	do {
		error = NULL;
		status = g_io_channel_read_line_string (ch, line, NULL, &error);

		if (status == G_IO_STATUS_NORMAL || G_IO_STATUS_EOF) {
			file = g_string_append (file, line->str);
		}
		else if (status == G_IO_STATUS_ERROR) {
			g_error ("Failed to read line from stdin: %s\n", error->message);
			g_error_free(error);

			error = NULL;
			status = g_io_channel_shutdown (ch, TRUE, &error);
			if (status == G_IO_STATUS_ERROR) {
				g_error ("Failed to shutdown io channel: %s\n", error->message);
				g_error_free (error);
			}
			return NULL;
		}
	} while (status != G_IO_STATUS_EOF);

	gchar *contents = g_strdup (file->str);
	g_string_free (line, TRUE);
	g_string_free (file, TRUE);

	return contents;
}

gint main (gint argc, gchar **argv) {

	GError *error = NULL;
	GOptionContext *context;
	GOptionGroup *group;

	gint retval = 0;

#if !GLIB_CHECK_VERSION(2,35,0)
	g_type_init ();
#endif

	/* Parse command line options */

	context = g_option_context_new("[source file]");
	g_option_context_set_summary(context, "Compile Sass (scss) files to CSS.");
	g_option_context_add_main_entries(context, entries, NULL);

	group = g_option_group_new("Gsassc Options", "These are the gsassc options", "", NULL, NULL);
	g_option_context_add_group(context, group);
	if (!g_option_context_parse(context, &argc, &argv, &error)){
		g_print("Option parsing failed: %s\n", error->message);
		exit(1);
	}

	if (show_version) {
		g_print ("gsassc version: %s\n", GSASSC_VERSION);
		return 0;
	}

	if (show_libsass_version) {
		printf("Libsass version: %s\n", libsass_version());
		return 0;
	}


	// concat all files and compile in one run
	gchar *input = NULL, *contents, *input_path;
	gsize len;
	GFile *infile;

	switch (argc) {

		case 0:
			g_error("Something went terribly wrong...");
			retval = -666;
			break;

		case 1:
			input = read_from_stdin ();
			input_path = "stdin";
		 	break;

		case 2:
			infile = g_file_new_for_commandline_arg (argv[1]);
			input_path = g_file_get_path (infile);

			g_print ("input_path = %s\n", input_path);

			// TODO: Proper error handling..!
			error = NULL;
			if (!g_file_load_contents (infile, NULL, &input, &len, NULL, &error)) {
				g_error ("Failed to read file: %s: %s\n", argv[1], error->message);
				input = NULL;
				g_error_free (error);
				retval = -1;
			}
			break;

		default:

			while (*++argv){

				error = NULL;

				g_print ("Reading file: %s\n", *argv);

				if (!g_file_get_contents(*argv, &contents, &len, &error)){
					g_error("Failed to read file: %s: %s\n", *argv, error->message);
					g_error_free(error);
					exit(-1);
				}
				if (input == NULL){
					input = contents;
				}
				else {
					input = g_strconcat(input, contents, NULL);
					g_free(contents);
				}
			}
			break;
	}


	if (input != NULL) {

		struct Sass_Data_Context *data_ctx = sass_make_data_context (input);
		struct Sass_Context *ctx = sass_data_context_get_context(data_ctx);
		struct Sass_Options *sass_options = sass_context_get_options (ctx);

		// Set Options

		sass_option_set_precision (sass_options, precision);

		sass_option_set_input_path (sass_options, input_path);


		if (!g_strcmp0 (style, "compressed")) {
			sass_option_set_output_style (sass_options, SASS_STYLE_COMPRESSED);
		}
		else if (!g_strcmp0(style, "nested")) {
			sass_option_set_output_style (sass_options, SASS_STYLE_NESTED);
		}
		else if (!g_strcmp0(style, "compact")) {
			sass_option_set_output_style (sass_options, SASS_STYLE_COMPACT);
		}
		else if (!g_strcmp0(style, "expanded")) {
			sass_option_set_output_style (sass_options, SASS_STYLE_EXPANDED);
		}
		else {
			g_error ("Illegal output style: %s\n", style);
			return -1;
		}

		if (include_paths != NULL) {
			sass_option_set_include_path(sass_options, include_paths);
		}
		sass_option_set_source_comments (sass_options, line_numbers);

		if (source_map) {
		 	sass_option_set_source_map_file (sass_options, input_path);
		}

		int status = sass_compile_data_context (data_ctx);

		if (status == 0) {
			const gchar *css = sass_context_get_output_string (ctx);
			if (outfile != NULL) {
				error = NULL;
				if (!g_file_set_contents (outfile, css, -1, &error)) {
					g_error ("Feild to write file: %s: %s\n", outfile, error->message);
					g_error_free (error);
					return -1;
				}
			}
			else {
				puts (sass_context_get_output_string (ctx));
			}
		}
		else {
			puts (sass_context_get_error_message (ctx));
		}

		sass_delete_data_context (data_ctx);

		// WEIRD: g_free'ing input will cause a »double freed« runtime error..?!?
		// g_free(input);
	}
	else if (retval == 0){
		g_print ("No input data.\n");
	}

	return retval;
}
