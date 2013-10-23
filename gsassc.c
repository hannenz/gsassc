#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <glib.h>
#include <gio/gio.h>
#include <sass_interface.h>

#define BUFSIZE 512

static gchar *outfile = NULL;
static gchar *style = "nested";
static gboolean line_numbers = FALSE;
static gboolean source_map = FALSE;
static gchar *include_paths = NULL;
static gchar *watch = NULL;


static GOptionEntry entries [] = {
	{	"output", 'o', 0, G_OPTION_ARG_FILENAME, &outfile, "Write to specified file", NULL},
	{	"style", 't', 0, G_OPTION_ARG_STRING, &style, "Output style. Can be nested or compressed" , NULL},
	{	"line-numbers", 'l', 0, G_OPTION_ARG_NONE, &line_numbers, "Emit comments showing original line numbers.", NULL},
	{	"source-map", 'g', 0, G_OPTION_ARG_NONE, &source_map, "Emit source map.", NULL},
	{	"import-path", 'I', 0, G_OPTION_ARG_STRING, &include_paths, "Set Sass import path (colon delimited list of paths).", NULL},
	{	"watch", 'w', 0, G_OPTION_ARG_STRING, &watch, "Watch a directory for changes. Defaults to the current dir", NULL},
	{ NULL }
};

gint output(gint error_status, gchar* error_message, gchar* output_string, gchar* outfile) {
	if (error_status) {
		if (error_message) {
			g_print("%s\n", error_message);
		}
		else {
			g_error("An error occured; no error message available");
		}
		return 1;

	}
	else if (output_string) {
		if (outfile) {

			GError *error = NULL;
			if (!g_file_set_contents(outfile, output_string, -1, &error)) {
				g_error("Failed to write to output file %s: %s", outfile, error->message);
				g_error_free(error);
				return 1;
			}
		}
		else {
			g_print("%s\n", output_string);
		}

		return 0;
	}
	else {
		g_error("Unknown internal error.\n");
		return 2;
	}
}


gint compile_file(struct sass_options options, gchar* input_path, gchar* outfile) {
	gint ret;

	struct sass_file_context *context = sass_new_file_context();
	context->options = options;
	context->input_path = input_path;

	sass_compile_file(context);

	ret = output(context->error_status, context->error_message, context->output_string, outfile);

	sass_free_file_context(context);
	return ret;
}


gint compile_stdin(struct sass_options options, gchar* outfile) {

	GIOChannel *ch;
	GString *file, *line;
	GError *error = NULL;
	GIOStatus status;
	gint ret;

	struct sass_context *ctx;


	file = g_string_new(NULL);
	line = g_string_new(NULL);
	ch = g_io_channel_unix_new(1);

	do {
		error = NULL;
		status = g_io_channel_read_line_string(ch, line, NULL, &error);

		if (status == G_IO_STATUS_NORMAL) {
			file = g_string_append(file, line->str);
		}
		else if (status == G_IO_STATUS_ERROR) {
			g_error("Failed to read line from stdin: %s\n", error->message);
			g_error_free(error);

			error = NULL;
			status = g_io_channel_shutdown(ch, TRUE, &error);
			if (status == G_IO_STATUS_ERROR){
				g_error("Failed to shutdown io channel: %s\n", error->message);
				g_error_free(error);
			}
			return 1;
		}
	}
	while (status != G_IO_STATUS_EOF);

	ctx = sass_new_context();
	ctx->options = options;
	ctx->source_string = file->str;
	sass_compile(ctx);
	ret = output(ctx->error_status, ctx->error_message, ctx->output_string, outfile);

	sass_free_context(ctx);

	g_string_free(line, TRUE);
	g_string_free(file, TRUE);

	return ret;
}

static void on_file_changed(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer udata) {

	gchar *file_path, *watchdir;
	const gchar *name;
	struct sass_options options;
	GError *error;
	GDir *dir;
	GList *non_partials;
	gint n;

	file_path = g_file_get_path(file);
	options = *(struct sass_options*)udata;

	watchdir = g_file_get_path(g_file_get_parent(file));

	if (g_str_has_prefix(file_path, "_")) {

		error = NULL;
		dir = g_dir_open(watchdir, 0, &error);
		if (dir == NULL){
			g_error("Failed to read directory: %s\n", error->message);
			g_error_free(error);
			exit(1);
		}
		
		non_partials = NULL;
		while ((name = g_dir_read_name(dir)) != NULL) {

			if (g_str_has_suffix(name, ".scss") && !g_str_has_prefix(name, "_")){
				non_partials = g_list_append(non_partials, (gchar*)name);
			}
		}
		g_dir_close(dir);

		n = g_list_length(non_partials);

		switch (n){
			case 0:
				g_error("No non-partial scss file in this directory: %s", watchdir);
				break;
			case 1:
			default:
				file_path = (gchar*)g_list_nth_data(non_partials, 1);
				break;
		}
	}

	compile_file(options, file_path, NULL);

	/*
	if IS_PARTIAL(file)
		NON_PARTALS = search for all non-partial files in the watched dir (get dir of file)
			if (count NON_PARTIALS == 1)
				compile NON_PARTIALS[0]
			else
				compile ???
			fi
	else
		compile the file
	fi
	*/

	return;

	switch (event_type)  {
		case G_FILE_MONITOR_EVENT_CHANGED:
		case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
		case G_FILE_MONITOR_EVENT_CREATED:
			compile_file(options, file_path, NULL);
			break;
		default:
			break;
	}
}

gint main (gint argc, gchar **argv) {

	GError *error = NULL;
	GOptionContext *context;
	GOptionGroup *group;
	GFileMonitor *monitor;

	g_type_init();


	/* Parse command line options */

	context = g_option_context_new("- compile sass files");
	g_option_context_add_main_entries(context, entries, NULL);

	group = g_option_group_new("My Options", "These are my options", "This is the help description for my options", NULL, NULL);
	g_option_context_add_group(context, group);
	if (!g_option_context_parse(context, &argc, &argv, &error)){
		g_print("Option parsing failed: %s\n", error->message);
		exit(1);
	}

	/* Setup libsass options */

	struct sass_options options;
	options.image_path = "images";
	options.include_paths = "";
	options.output_style = SASS_STYLE_NESTED;

	if (!g_strcmp0(style, "compressed")) {
		options.output_style = SASS_STYLE_COMPRESSED;
	}
	else if (!g_strcmp0(style, "nested")) {
		options.output_style = SASS_STYLE_NESTED;
	}
	else {
		g_print("Illegal output style: %s\n", style);
		exit(1);
	}

	options.include_paths = include_paths;
	if (line_numbers){
		options.source_comments = SASS_SOURCE_COMMENTS_DEFAULT;
	}
	if (source_map){
		options.source_comments = SASS_SOURCE_COMMENTS_MAP;
	}


	if (watch != NULL) {

		error = NULL;
		if ((monitor = g_file_monitor(g_file_new_for_path(watch), 0, NULL, &error)) == NULL) {
			g_error("Failed to setup a file monitor: %s\n", error->message);
			g_error_free(error);
			exit(1);
		}

		g_signal_connect(monitor, "changed", G_CALLBACK(on_file_changed), &options);

		GMainLoop *loop;
		loop = g_main_loop_new(NULL, FALSE);
		g_main_loop_run(loop);
		return 0;
	}


	if (argc == 2) {
		compile_file(options, argv[1], outfile);
	}
	else if (argc == 1) {
		compile_stdin(options, outfile);
	}

	return 0;
}
